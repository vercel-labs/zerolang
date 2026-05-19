#include "zero.h"

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Scope Scope;

typedef struct {
  char *root;
  Scope *root_scope;
  char *path;
} Place;

typedef struct {
  Place *items;
  size_t len;
  size_t cap;
} PlaceVec;

typedef struct {
  char *value_path;
  Place origin;
  bool mutable_borrow;
  bool local_storage;
} ProvenanceEntry;

typedef struct {
  ProvenanceEntry *items;
  size_t len;
  size_t cap;
} ValueProvenance;

typedef struct {
  Place target;
  ValueProvenance value;
  bool overwrite;
} ProvenanceStorageEffect;

typedef struct {
  ProvenanceStorageEffect *items;
  size_t len;
  size_t cap;
} ProvenanceStorageEffectVec;

typedef struct {
  ValueProvenance return_value;
  ProvenanceStorageEffectVec storage_effects;
  bool may_return;
  bool return_complete;
  bool effect_complete;
  bool callee_local_storage;
} FunctionProvenanceSummary;

typedef struct ProvenanceScopeSnapshot {
  Scope *scope;
  ValueProvenance *origins;
  size_t len;
  struct ProvenanceScopeSnapshot *next;
} ProvenanceScopeSnapshot;

struct Scope {
  char **names;
  char **types;
  bool *mutable;
  bool *moved;
  bool *is_param;
  int *decl_line;
  int *decl_column;
  ValueProvenance *value_provenance;
  size_t len;
  size_t cap;
  Scope *parent;
};

typedef enum {
  META_VALUE_NUMBER,
  META_VALUE_BOOL,
  META_VALUE_STRING
} MetaValueKind;

typedef struct {
  MetaValueKind kind;
  unsigned long long number;
  bool boolean;
  char text[128];
} MetaValue;

typedef enum {
  STATIC_VALUE_INVALID,
  STATIC_VALUE_INTEGER,
  STATIC_VALUE_BOOL,
  STATIC_VALUE_ENUM
} StaticValueKind;

typedef struct {
  StaticValueKind kind;
  unsigned long long number;
  bool boolean;
  char enum_type[64];
  char enum_case[64];
} StaticValue;

typedef struct MetaCacheEntry {
  char *key;
  MetaValue value;
  struct MetaCacheEntry *next;
} MetaCacheEntry;

static const ZTargetInfo *current_check_target = NULL;
static MetaCacheEntry *meta_cache = NULL;
static ZMetaCacheStats meta_stats = {0};

static void *checker_grow_items(void *items, size_t len, size_t *cap, size_t initial, size_t item_size) {
  if (len + 1 > *cap) {
    *cap = z_grow_capacity(*cap, len + 1, initial);
    return z_checked_reallocarray(items, *cap, item_size);
  }
  return items;
}

static const char *origin_path_text(const char *path) {
  return path ? path : "";
}

static bool origin_path_equal(const char *left, const char *right) {
  return strcmp(origin_path_text(left), origin_path_text(right)) == 0;
}

static bool origin_path_segment_boundary(char ch) {
  return ch == '.' || ch == '[';
}

static bool origin_path_array_segment_match(const char **path_cursor, const char **prefix_cursor) {
  const char *path = *path_cursor;
  const char *prefix = *prefix_cursor;
  const char *path_end = strchr(path, ']');
  const char *prefix_end = strchr(prefix, ']');
  if (!path_end || !prefix_end) return false;
  bool path_wildcard = path_end == path + 2 && path[1] == '*';
  bool prefix_wildcard = prefix_end == prefix + 2 && prefix[1] == '*';
  bool match = path_wildcard || prefix_wildcard ||
    ((size_t)(path_end - path) == (size_t)(prefix_end - prefix) &&
     strncmp(path, prefix, (size_t)(path_end - path + 1)) == 0);
  if (!match) return false;
  *path_cursor = path_end + 1;
  *prefix_cursor = prefix_end + 1;
  return true;
}

static bool origin_path_array_segment_definitely_within(const char **path_cursor, const char **prefix_cursor) {
  const char *path = *path_cursor;
  const char *prefix = *prefix_cursor;
  const char *path_end = strchr(path, ']');
  const char *prefix_end = strchr(prefix, ']');
  if (!path_end || !prefix_end) return false;
  bool path_wildcard = path_end == path + 2 && path[1] == '*';
  bool prefix_wildcard = prefix_end == prefix + 2 && prefix[1] == '*';
  bool match = prefix_wildcard ||
    (!path_wildcard &&
     (size_t)(path_end - path) == (size_t)(prefix_end - prefix) &&
     strncmp(path, prefix, (size_t)(path_end - path + 1)) == 0);
  if (!match) return false;
  *path_cursor = path_end + 1;
  *prefix_cursor = prefix_end + 1;
  return true;
}

static bool origin_path_prefix_match(const char *path, const char *prefix, const char **path_after_prefix) {
  while (*prefix) {
    if (*path == '[' && *prefix == '[') {
      if (!origin_path_array_segment_match(&path, &prefix)) return false;
      continue;
    }
    if (*path != *prefix) return false;
    path++;
    prefix++;
  }
  if (path_after_prefix) *path_after_prefix = path;
  return true;
}

static bool origin_path_definite_prefix_match(const char *path, const char *prefix, const char **path_after_prefix) {
  while (*prefix) {
    if (*path == '[' && *prefix == '[') {
      if (!origin_path_array_segment_definitely_within(&path, &prefix)) return false;
      continue;
    }
    if (*path != *prefix) return false;
    path++;
    prefix++;
  }
  if (path_after_prefix) *path_after_prefix = path;
  return true;
}

static bool origin_path_is_within(const char *path, const char *parent) {
  const char *actual = origin_path_text(path);
  const char *prefix = origin_path_text(parent);
  if (!prefix[0]) return true;
  const char *after = NULL;
  if (!origin_path_prefix_match(actual, prefix, &after)) return false;
  return !after[0] || origin_path_segment_boundary(after[0]);
}

static bool origin_path_is_definitely_within(const char *path, const char *parent) {
  const char *actual = origin_path_text(path);
  const char *prefix = origin_path_text(parent);
  if (!prefix[0]) return true;
  const char *after = NULL;
  if (!origin_path_definite_prefix_match(actual, prefix, &after)) return false;
  return !after[0] || origin_path_segment_boundary(after[0]);
}

static bool origin_path_overlaps(const char *left, const char *right) {
  return origin_path_is_within(left, right) || origin_path_is_within(right, left);
}

static void format_origin_place(char *out, size_t out_len, const char *root, const char *path) {
  if (!out || out_len == 0) return;
  const char *actual_path = origin_path_text(path);
  const char *separator = actual_path[0] && actual_path[0] != '[' ? "." : "";
  snprintf(out, out_len, "%.96s%s%.96s", root ? root : "", separator, actual_path);
}

static const char *origin_path_after_prefix(const char *path, const char *prefix) {
  const char *actual = origin_path_text(path);
  const char *parent = origin_path_text(prefix);
  if (!parent[0]) return actual;
  const char *after = NULL;
  if (origin_path_prefix_match(actual, parent, &after)) {
    if (!after[0]) return "";
    if (after[0] == '.') return after + 1;
    if (after[0] == '[') return after;
  }
  return actual;
}

static char *origin_path_join(const char *prefix, const char *suffix) {
  const char *left = origin_path_text(prefix);
  const char *right = origin_path_text(suffix);
  if (!left[0]) return right[0] ? z_strdup(right) : NULL;
  if (!right[0]) return z_strdup(left);
  size_t left_len = strlen(left);
  size_t right_len = strlen(right);
  const char *separator = right[0] == '[' ? "" : ".";
  char *joined = z_checked_malloc(left_len + right_len + strlen(separator) + 1);
  snprintf(joined, left_len + right_len + strlen(separator) + 1, "%s%s%s", left, separator, right);
  return joined;
}

static bool value_provenance_add_full(ValueProvenance *origins, const char *root, Scope *root_scope, bool mut_borrow, bool local_storage, const char *path, const char *origin_path) {
  if (!origins || !root || !root[0]) return false;
  for (size_t i = 0; i < origins->len; i++) {
    ProvenanceEntry *entry = &origins->items[i];
    if (strcmp(entry->origin.root, root) == 0 && entry->origin.root_scope == root_scope &&
        origin_path_equal(entry->value_path, path) && origin_path_equal(entry->origin.path, origin_path)) {
      entry->mutable_borrow = entry->mutable_borrow || mut_borrow;
      entry->local_storage = entry->local_storage || local_storage;
      return true;
    }
  }
  origins->items = checker_grow_items(origins->items, origins->len, &origins->cap, 4, sizeof(ProvenanceEntry));
  origins->items[origins->len] = (ProvenanceEntry){
    .value_path = path && path[0] ? z_strdup(path) : NULL,
    .origin = {
      .root = z_strdup(root),
      .root_scope = root_scope,
      .path = origin_path && origin_path[0] ? z_strdup(origin_path) : NULL,
    },
    .mutable_borrow = mut_borrow,
    .local_storage = local_storage,
  };
  origins->len++;
  return true;
}

static bool value_provenance_add_path(ValueProvenance *origins, const char *root, Scope *root_scope, bool mut_borrow, bool local_storage, const char *path) {
  return value_provenance_add_full(origins, root, root_scope, mut_borrow, local_storage, path, NULL);
}

static bool value_provenance_add(ValueProvenance *origins, const char *root, Scope *root_scope, bool mut_borrow, bool local_storage) {
  return value_provenance_add_path(origins, root, root_scope, mut_borrow, local_storage, NULL);
}

static bool value_provenance_add_all(ValueProvenance *out, const ValueProvenance *source) {
  bool added = false;
  if (!out || !source) return false;
  for (size_t i = 0; i < source->len; i++) {
    ProvenanceEntry *entry = &source->items[i];
    if (value_provenance_add_full(out, entry->origin.root, entry->origin.root_scope, entry->mutable_borrow, entry->local_storage, entry->value_path, entry->origin.path)) added = true;
  }
  return added;
}

static bool value_provenance_add_all_with_prefix(ValueProvenance *out, const ValueProvenance *source, const char *path_prefix) {
  bool added = false;
  if (!out || !source) return false;
  for (size_t i = 0; i < source->len; i++) {
    ProvenanceEntry *entry = &source->items[i];
    char *path = origin_path_join(path_prefix, entry->value_path);
    if (value_provenance_add_full(out, entry->origin.root, entry->origin.root_scope, entry->mutable_borrow, entry->local_storage, path, entry->origin.path)) added = true;
    free(path);
  }
  return added;
}

static bool value_provenance_add_all_as_with_prefix(ValueProvenance *out, const ValueProvenance *source, bool mut_borrow, const char *path_prefix) {
  bool added = false;
  if (!out || !source) return false;
  for (size_t i = 0; i < source->len; i++) {
    ProvenanceEntry *entry = &source->items[i];
    char *path = origin_path_join(path_prefix, entry->value_path);
    if (value_provenance_add_full(out, entry->origin.root, entry->origin.root_scope, mut_borrow, entry->local_storage, path, entry->origin.path)) added = true;
    free(path);
  }
  return added;
}

static bool value_provenance_add_all_as_with_origin_suffix(ValueProvenance *out, const ValueProvenance *source, bool mut_borrow, const char *value_prefix, const char *origin_suffix) {
  bool added = false;
  if (!out || !source) return false;
  for (size_t i = 0; i < source->len; i++) {
    ProvenanceEntry *entry = &source->items[i];
    char *value_path = origin_path_join(value_prefix, entry->value_path);
    char *origin_path = origin_path_join(entry->origin.path, origin_suffix);
    if (value_provenance_add_full(out, entry->origin.root, entry->origin.root_scope, mut_borrow, entry->local_storage, value_path, origin_path)) added = true;
    free(value_path);
    free(origin_path);
  }
  return added;
}

static bool value_provenance_add_direct_as_with_origin_suffix(ValueProvenance *out, const ValueProvenance *source, bool mut_borrow, const char *origin_suffix) {
  bool added = false;
  if (!out || !source) return false;
  for (size_t i = 0; i < source->len; i++) {
    ProvenanceEntry *entry = &source->items[i];
    if (origin_path_text(entry->value_path)[0]) continue;
    char *origin_path = origin_path_join(entry->origin.path, origin_suffix);
    if (value_provenance_add_full(out, entry->origin.root, entry->origin.root_scope, mut_borrow, entry->local_storage, NULL, origin_path)) added = true;
    free(origin_path);
  }
  return added;
}

static bool value_provenance_add_all_under_path(ValueProvenance *out, const ValueProvenance *source, const char *path_prefix) {
  bool added = false;
  if (!out || !source) return false;
  for (size_t i = 0; i < source->len; i++) {
    ProvenanceEntry *entry = &source->items[i];
    if (!origin_path_is_within(entry->value_path, path_prefix)) continue;
    const char *relative_path = origin_path_after_prefix(entry->value_path, path_prefix);
    if (value_provenance_add_full(out, entry->origin.root, entry->origin.root_scope, entry->mutable_borrow, entry->local_storage, relative_path, entry->origin.path)) added = true;
  }
  return added;
}

static void value_provenance_free(ValueProvenance *origins) {
  if (!origins) return;
  for (size_t i = 0; i < origins->len; i++) {
    free(origins->items[i].origin.root);
    free(origins->items[i].origin.path);
    free(origins->items[i].value_path);
  }
  free(origins->items);
  origins->items = NULL;
  origins->len = 0;
  origins->cap = 0;
}

static bool place_vec_add(PlaceVec *places, const char *root, Scope *root_scope, const char *path) {
  if (!places || !root || !root[0]) return false;
  for (size_t i = 0; i < places->len; i++) {
    Place *place = &places->items[i];
    if (strcmp(place->root, root) == 0 && place->root_scope == root_scope && origin_path_equal(place->path, path)) return true;
  }
  places->items = checker_grow_items(places->items, places->len, &places->cap, 4, sizeof(Place));
  places->items[places->len++] = (Place){
    .root = z_strdup(root),
    .root_scope = root_scope,
    .path = path && path[0] ? z_strdup(path) : NULL,
  };
  return true;
}

static void place_free(Place *place) {
  if (!place) return;
  free(place->root);
  free(place->path);
  *place = (Place){0};
}

static void place_vec_free(PlaceVec *places) {
  if (!places) return;
  for (size_t i = 0; i < places->len; i++) {
    place_free(&places->items[i]);
  }
  free(places->items);
  places->items = NULL;
  places->len = 0;
  places->cap = 0;
}

static bool provenance_storage_effect_vec_add(ProvenanceStorageEffectVec *effects, const char *target_root, Scope *target_scope, const char *target_path, const ValueProvenance *value, bool overwrite) {
  if (!effects || !target_root || !target_root[0] || !value || value->len == 0) return false;
  effects->items = checker_grow_items(effects->items, effects->len, &effects->cap, 4, sizeof(ProvenanceStorageEffect));
  ProvenanceStorageEffect *effect = &effects->items[effects->len++];
  *effect = (ProvenanceStorageEffect){
    .target = {
      .root = z_strdup(target_root),
      .root_scope = target_scope,
      .path = target_path && target_path[0] ? z_strdup(target_path) : NULL,
    },
    .value = {0},
    .overwrite = overwrite,
  };
  value_provenance_add_all(&effect->value, value);
  return true;
}

static void provenance_storage_effect_vec_free(ProvenanceStorageEffectVec *effects) {
  if (!effects) return;
  for (size_t i = 0; i < effects->len; i++) {
    place_free(&effects->items[i].target);
    value_provenance_free(&effects->items[i].value);
  }
  free(effects->items);
  effects->items = NULL;
  effects->len = 0;
  effects->cap = 0;
}

static void function_provenance_summary_free(FunctionProvenanceSummary *summary) {
  if (!summary) return;
  value_provenance_free(&summary->return_value);
  provenance_storage_effect_vec_free(&summary->storage_effects);
  *summary = (FunctionProvenanceSummary){0};
}

void z_set_check_target(const ZTargetInfo *target) {
  current_check_target = target;
}

ZMetaCacheStats z_meta_cache_stats(void) {
  return meta_stats;
}

static void scope_add_ex(Scope *scope, const char *name, const char *type, bool mutable, bool is_param, int line, int column) {
  if (scope->len + 1 > scope->cap) {
    size_t next_cap = z_grow_capacity(scope->cap, scope->len + 1, 8);
    scope->names = z_checked_reallocarray(scope->names, next_cap, sizeof(char *));
    scope->types = z_checked_reallocarray(scope->types, next_cap, sizeof(char *));
    scope->mutable = z_checked_reallocarray(scope->mutable, next_cap, sizeof(bool));
    scope->moved = z_checked_reallocarray(scope->moved, next_cap, sizeof(bool));
    scope->is_param = z_checked_reallocarray(scope->is_param, next_cap, sizeof(bool));
    scope->decl_line = z_checked_reallocarray(scope->decl_line, next_cap, sizeof(int));
    scope->decl_column = z_checked_reallocarray(scope->decl_column, next_cap, sizeof(int));
    scope->value_provenance = z_checked_reallocarray(scope->value_provenance, next_cap, sizeof(ValueProvenance));
    scope->cap = next_cap;
  }
  scope->names[scope->len++] = z_strdup(name);
  scope->types[scope->len - 1] = z_strdup(type ? type : "Unknown");
  scope->mutable[scope->len - 1] = mutable;
  scope->moved[scope->len - 1] = false;
  scope->is_param[scope->len - 1] = is_param;
  scope->decl_line[scope->len - 1] = line;
  scope->decl_column[scope->len - 1] = column;
  scope->value_provenance[scope->len - 1] = (ValueProvenance){0};
}

static void scope_add(Scope *scope, const char *name, const char *type, bool mutable) {
  scope_add_ex(scope, name, type, mutable, false, 0, 0);
}

static void scope_add_decl(Scope *scope, const char *name, const char *type, bool mutable, int line, int column) {
  scope_add_ex(scope, name, type, mutable, false, line, column);
}

static void scope_add_param_decl(Scope *scope, const char *name, const char *type, int line, int column) {
  scope_add_ex(scope, name, type, false, true, line, column);
}

static bool scope_has(Scope *scope, const char *name) {
  for (Scope *cursor = scope; cursor; cursor = cursor->parent) {
    for (size_t i = 0; i < cursor->len; i++) {
      if (strcmp(cursor->names[i], name) == 0) return true;
    }
  }
  return false;
}

static Scope *scope_binding_scope(Scope *scope, const char *name) {
  for (Scope *cursor = scope; cursor; cursor = cursor->parent) {
    for (size_t i = 0; i < cursor->len; i++) {
      if (strcmp(cursor->names[i], name) == 0) return cursor;
    }
  }
  return NULL;
}

static bool scope_binding_index_in_scope(Scope *scope, const char *name, size_t *out_index) {
  if (!scope || !name) return false;
  for (size_t i = 0; i < scope->len; i++) {
    if (strcmp(scope->names[i], name) == 0) {
      if (out_index) *out_index = i;
      return true;
    }
  }
  return false;
}

static bool scope_is_ancestor_or_self(const Scope *candidate, const Scope *scope) {
  for (const Scope *cursor = scope; cursor; cursor = cursor->parent) {
    if (cursor == candidate) return true;
  }
  return false;
}

static const char *scope_type(Scope *scope, const char *name) {
  for (Scope *cursor = scope; cursor; cursor = cursor->parent) {
    for (size_t i = 0; i < cursor->len; i++) {
      if (strcmp(cursor->names[i], name) == 0) return cursor->types[i];
    }
  }
  return NULL;
}

static const char *scope_type_in_binding_scope(Scope *scope, Scope *binding_scope, const char *name) {
  if (binding_scope) {
    size_t index = 0;
    if (scope_binding_index_in_scope(binding_scope, name, &index)) return binding_scope->types[index];
  }
  return scope_type(scope, name);
}

static bool scope_is_mutable(Scope *scope, const char *name) {
  for (Scope *cursor = scope; cursor; cursor = cursor->parent) {
    for (size_t i = 0; i < cursor->len; i++) {
      if (strcmp(cursor->names[i], name) == 0) return cursor->mutable[i];
    }
  }
  return false;
}

static bool scope_is_moved(Scope *scope, const char *name) {
  for (Scope *cursor = scope; cursor; cursor = cursor->parent) {
    for (size_t i = 0; i < cursor->len; i++) {
      if (strcmp(cursor->names[i], name) == 0) return cursor->moved[i];
    }
  }
  return false;
}

static bool scope_set_moved(Scope *scope, const char *name, bool moved) {
  for (Scope *cursor = scope; cursor; cursor = cursor->parent) {
    for (size_t i = 0; i < cursor->len; i++) {
      if (strcmp(cursor->names[i], name) == 0) {
        cursor->moved[i] = moved;
        return true;
      }
    }
  }
  return false;
}

static bool scope_is_param(Scope *scope, const char *name) {
  for (Scope *cursor = scope; cursor; cursor = cursor->parent) {
    for (size_t i = 0; i < cursor->len; i++) {
      if (strcmp(cursor->names[i], name) == 0) return cursor->is_param[i];
    }
  }
  return false;
}

static bool scope_borrow_counts_for_place(Scope *scope, const char *root, const char *path, size_t *shared, size_t *mut) {
  if (shared) *shared = 0;
  if (mut) *mut = 0;
  if (!scope || !root) return false;
  bool found = false;
  Scope *root_scope = scope_binding_scope(scope, root);
  for (Scope *cursor = scope; cursor; cursor = cursor->parent) {
    for (size_t binding_index = 0; binding_index < cursor->len; binding_index++) {
      ValueProvenance *origins = &cursor->value_provenance[binding_index];
      for (size_t origin_index = 0; origin_index < origins->len; origin_index++) {
        ProvenanceEntry *entry = &origins->items[origin_index];
        if (strcmp(entry->origin.root, root) != 0) continue;
        if (root_scope && entry->origin.root_scope && entry->origin.root_scope != root_scope) continue;
        if (!origin_path_overlaps(entry->origin.path, path)) continue;
        if (entry->mutable_borrow) {
          if (mut) (*mut)++;
        } else {
          if (shared) (*shared)++;
        }
        found = true;
      }
    }
  }
  return found;
}

static bool borrow_trace_equal(const ZBorrowTrace *left, const ZBorrowTrace *right) {
  return left && right &&
         strcmp(left->root, right->root) == 0 &&
         strcmp(left->path, right->path) == 0 &&
         strcmp(left->kind, right->kind) == 0 &&
         strcmp(left->binding, right->binding) == 0 &&
         left->binding_line == right->binding_line &&
         left->binding_column == right->binding_column;
}

static bool borrow_trace_push(ZBorrowTrace *out, size_t *out_len, bool *truncated, const ZBorrowTrace *trace) {
  if (!out || !out_len || !trace || !trace->root[0]) return false;
  for (size_t i = 0; i < *out_len; i++) {
    if (borrow_trace_equal(&out[i], trace)) return true;
  }
  if (*out_len >= Z_BORROW_TRACE_MAX) {
    if (truncated) *truncated = true;
    return false;
  }
  out[*out_len] = *trace;
  (*out_len)++;
  return true;
}

static bool scope_active_borrows_for_place(Scope *scope, const char *root, const char *path, bool mutable_only, ZBorrowTrace *out, size_t *out_len, bool *truncated) {
  if (!scope || !root || !out || !out_len) return false;
  *out_len = 0;
  if (truncated) *truncated = false;
  bool found = false;
  Scope *root_scope = scope_binding_scope(scope, root);
  for (Scope *cursor = scope; cursor; cursor = cursor->parent) {
    for (size_t binding_index = 0; binding_index < cursor->len; binding_index++) {
      ValueProvenance *origins = &cursor->value_provenance[binding_index];
      for (size_t origin_index = 0; origin_index < origins->len; origin_index++) {
        ProvenanceEntry *entry = &origins->items[origin_index];
        if (strcmp(entry->origin.root, root) != 0) continue;
        if (root_scope && entry->origin.root_scope && entry->origin.root_scope != root_scope) continue;
        if (!origin_path_overlaps(entry->origin.path, path)) continue;
        if (mutable_only && !entry->mutable_borrow) continue;
        ZBorrowTrace trace = {0};
        snprintf(trace.root, sizeof(trace.root), "%s", entry->origin.root);
        snprintf(trace.path, sizeof(trace.path), "%s", origin_path_text(entry->origin.path));
        snprintf(trace.kind, sizeof(trace.kind), "%s", entry->mutable_borrow ? "mutable" : "shared");
        snprintf(trace.binding, sizeof(trace.binding), "%s", cursor->names[binding_index]);
        trace.binding_line = cursor->decl_line ? cursor->decl_line[binding_index] : 0;
        trace.binding_column = cursor->decl_column ? cursor->decl_column[binding_index] : 0;
        borrow_trace_push(out, out_len, truncated, &trace);
        found = true;
      }
    }
  }
  return found;
}

static void set_diag_borrow_trace(ZDiag *diag, const ZBorrowTrace *active, size_t active_len, bool truncated, const char *repair) {
  if (!diag || !active || active_len == 0) return;
  size_t copy_len = active_len > Z_BORROW_TRACE_MAX ? Z_BORROW_TRACE_MAX : active_len;
  for (size_t i = 0; i < copy_len; i++) diag->borrow_traces[i] = active[i];
  diag->borrow_trace_count = copy_len;
  diag->borrow_trace_truncated = truncated || active_len > Z_BORROW_TRACE_MAX;
  snprintf(diag->borrow_repair, sizeof(diag->borrow_repair), "%s", repair ? repair : "end the active lexical borrow before the conflicting operation");
}

static void scope_clear_value_provenance_at(ValueProvenance *origins) {
  if (!origins) return;
  value_provenance_free(origins);
}

static void scope_replace_value_provenance_at(Scope *lookup_scope, Scope *binding_scope, size_t index, const ValueProvenance *origins) {
  if (!binding_scope || index >= binding_scope->len) return;
  (void)lookup_scope;
  scope_clear_value_provenance_at(&binding_scope->value_provenance[index]);
  if (!origins) return;
  for (size_t origin_index = 0; origin_index < origins->len; origin_index++) {
    ProvenanceEntry *entry = &origins->items[origin_index];
    value_provenance_add_full(&binding_scope->value_provenance[index], entry->origin.root, entry->origin.root_scope, entry->mutable_borrow, entry->local_storage, entry->value_path, entry->origin.path);
  }
}

static void scope_replace_value_provenance_path_at(Scope *lookup_scope, Scope *binding_scope, size_t index, const char *path, const ValueProvenance *origins) {
  if (!binding_scope || index >= binding_scope->len) return;
  ValueProvenance merged = {0};
  ValueProvenance *existing = &binding_scope->value_provenance[index];
  for (size_t i = 0; i < existing->len; i++) {
    ProvenanceEntry *entry = &existing->items[i];
    if (origin_path_is_definitely_within(entry->value_path, path)) continue;
    value_provenance_add_full(&merged, entry->origin.root, entry->origin.root_scope, entry->mutable_borrow, entry->local_storage, entry->value_path, entry->origin.path);
  }
  value_provenance_add_all_with_prefix(&merged, origins, path);
  scope_replace_value_provenance_at(lookup_scope, binding_scope, index, &merged);
  value_provenance_free(&merged);
}

static void scope_set_value_provenance(Scope *scope, const char *name, const ValueProvenance *origins) {
  if (!scope || !name || !origins) return;
  for (Scope *cursor = scope; cursor; cursor = cursor->parent) {
    for (size_t i = 0; i < cursor->len; i++) {
      if (strcmp(cursor->names[i], name) == 0) {
        scope_replace_value_provenance_at(scope, cursor, i, origins);
        return;
      }
    }
  }
}

static void scope_set_value_provenance_path(Scope *scope, const char *name, const char *path, const ValueProvenance *origins) {
  if (!scope || !name || !origins) return;
  for (Scope *cursor = scope; cursor; cursor = cursor->parent) {
    for (size_t i = 0; i < cursor->len; i++) {
      if (strcmp(cursor->names[i], name) == 0) {
        scope_replace_value_provenance_path_at(scope, cursor, i, path, origins);
        return;
      }
    }
  }
}

static void scope_set_value_provenance_path_in_scope(Scope *lookup_scope, Scope *binding_scope, const char *name, const char *path, const ValueProvenance *origins) {
  if (!lookup_scope || !name || !origins) return;
  if (binding_scope) {
    size_t index = 0;
    if (scope_binding_index_in_scope(binding_scope, name, &index)) {
      scope_replace_value_provenance_path_at(lookup_scope, binding_scope, index, path, origins);
      return;
    }
  }
  scope_set_value_provenance_path(lookup_scope, name, path, origins);
}

static bool scope_copy_value_provenance(Scope *scope, const char *name, ValueProvenance *out) {
  bool added = false;
  for (Scope *cursor = scope; cursor; cursor = cursor->parent) {
    for (size_t i = 0; i < cursor->len; i++) {
      if (strcmp(cursor->names[i], name) == 0) {
        for (size_t origin_index = 0; origin_index < cursor->value_provenance[i].len; origin_index++) {
          ProvenanceEntry *entry = &cursor->value_provenance[i].items[origin_index];
          if (value_provenance_add_full(out, entry->origin.root, entry->origin.root_scope, entry->mutable_borrow, entry->local_storage, entry->value_path, entry->origin.path)) added = true;
        }
        return added;
      }
    }
  }
  return false;
}

static bool scope_copy_value_provenance_from_scope(Scope *scope, Scope *binding_scope, const char *name, ValueProvenance *out) {
  if (binding_scope) {
    size_t index = 0;
    bool added = false;
    if (!scope_binding_index_in_scope(binding_scope, name, &index)) return false;
    for (size_t origin_index = 0; origin_index < binding_scope->value_provenance[index].len; origin_index++) {
      ProvenanceEntry *entry = &binding_scope->value_provenance[index].items[origin_index];
      if (value_provenance_add_full(out, entry->origin.root, entry->origin.root_scope, entry->mutable_borrow, entry->local_storage, entry->value_path, entry->origin.path)) added = true;
    }
    return added;
  }
  return scope_copy_value_provenance(scope, name, out);
}

static ProvenanceScopeSnapshot *provenance_scope_snapshot_capture(Scope *scope) {
  ProvenanceScopeSnapshot *head = NULL;
  ProvenanceScopeSnapshot *tail = NULL;
  for (Scope *cursor = scope; cursor; cursor = cursor->parent) {
    ProvenanceScopeSnapshot *frame = z_checked_calloc(1, sizeof(ProvenanceScopeSnapshot));
    frame->scope = cursor;
    frame->len = cursor->len;
    frame->origins = z_checked_calloc(frame->len, sizeof(ValueProvenance));
    for (size_t i = 0; i < frame->len; i++) {
      value_provenance_add_all(&frame->origins[i], &cursor->value_provenance[i]);
    }
    if (!head) {
      head = frame;
    } else {
      tail->next = frame;
    }
    tail = frame;
  }
  return head;
}

static const ProvenanceScopeSnapshot *provenance_scope_snapshot_find(const ProvenanceScopeSnapshot *snapshot, const Scope *scope) {
  for (const ProvenanceScopeSnapshot *frame = snapshot; frame; frame = frame->next) {
    if (frame->scope == scope) return frame;
  }
  return NULL;
}

static void provenance_scope_snapshot_restore(const ProvenanceScopeSnapshot *snapshot) {
  for (const ProvenanceScopeSnapshot *frame = snapshot; frame; frame = frame->next) {
    if (!frame->scope) continue;
    size_t len = frame->len < frame->scope->len ? frame->len : frame->scope->len;
    for (size_t i = 0; i < len; i++) {
      scope_replace_value_provenance_at(frame->scope, frame->scope, i, &frame->origins[i]);
    }
  }
}

static void provenance_scope_snapshot_restore_union(const ProvenanceScopeSnapshot *base, ProvenanceScopeSnapshot *const *states, const bool *include, size_t state_len) {
  bool any_included = false;
  for (size_t state_index = 0; include && state_index < state_len; state_index++) {
    if (include[state_index]) any_included = true;
  }
  if (!any_included) {
    provenance_scope_snapshot_restore(base);
    return;
  }
  for (const ProvenanceScopeSnapshot *base_frame = base; base_frame; base_frame = base_frame->next) {
    if (!base_frame->scope) continue;
    size_t len = base_frame->len < base_frame->scope->len ? base_frame->len : base_frame->scope->len;
    for (size_t binding_index = 0; binding_index < len; binding_index++) {
      ValueProvenance merged = {0};
      for (size_t state_index = 0; state_index < state_len; state_index++) {
        if (!include[state_index]) continue;
        const ProvenanceScopeSnapshot *state_frame = provenance_scope_snapshot_find(states[state_index], base_frame->scope);
        if (state_frame && binding_index < state_frame->len) {
          value_provenance_add_all(&merged, &state_frame->origins[binding_index]);
        } else {
          value_provenance_add_all(&merged, &base_frame->origins[binding_index]);
        }
      }
      scope_replace_value_provenance_at(base_frame->scope, base_frame->scope, binding_index, &merged);
      value_provenance_free(&merged);
    }
  }
}

static void provenance_scope_snapshot_restore_optional_branch(ProvenanceScopeSnapshot *before, ProvenanceScopeSnapshot *after, bool include_before, bool include_after) {
  ProvenanceScopeSnapshot *states[] = {before, after};
  bool include[] = {include_before, include_after};
  provenance_scope_snapshot_restore_union(before, states, include, 2);
}

static void provenance_scope_snapshot_free(ProvenanceScopeSnapshot *snapshot) {
  while (snapshot) {
    ProvenanceScopeSnapshot *next = snapshot->next;
    for (size_t i = 0; i < snapshot->len; i++) value_provenance_free(&snapshot->origins[i]);
    free(snapshot->origins);
    free(snapshot);
    snapshot = next;
  }
}

static void scope_free(Scope *scope) {
  for (size_t i = 0; i < scope->len; i++) {
    free(scope->names[i]);
    free(scope->types[i]);
    value_provenance_free(&scope->value_provenance[i]);
  }
  free(scope->names);
  free(scope->types);
  free(scope->mutable);
  free(scope->moved);
  free(scope->is_param);
  free(scope->decl_line);
  free(scope->decl_column);
  free(scope->value_provenance);
}

static bool set_diag(ZDiag *diag, int code, const char *message, int line, int column) {
  diag->code = code;
  diag->line = line;
  diag->column = column;
  diag->length = 1;
  diag->borrow_trace_count = 0;
  diag->borrow_trace_truncated = false;
  diag->borrow_repair[0] = 0;
  snprintf(diag->message, sizeof(diag->message), "%s", message);
  return false;
}

static bool set_diag_detail(
  ZDiag *diag,
  int code,
  const char *message,
  int line,
  int column,
  const char *expected,
  const char *actual,
  const char *help
) {
  set_diag(diag, code, message, line, column);
  if (expected) snprintf(diag->expected, sizeof(diag->expected), "%s", expected);
  if (actual) snprintf(diag->actual, sizeof(diag->actual), "%s", actual);
  if (help) snprintf(diag->help, sizeof(diag->help), "%s", help);
  return false;
}

static bool is_builtin_value(const char *name) {
  return strcmp(name, "std") == 0;
}

static void member_name_buf(const Expr *expr, ZBuf *buf) {
  if (!expr) return;
  if (expr->kind == EXPR_IDENT) {
    zbuf_append(buf, expr->text);
  } else if (expr->kind == EXPR_MEMBER) {
    member_name_buf(expr->left, buf);
    zbuf_append_char(buf, '.');
    zbuf_append(buf, expr->text);
  }
}

static bool is_world_stream_write_callee(const Expr *callee, Scope *scope) {
  if (!callee || callee->kind != EXPR_MEMBER || !callee->text || strcmp(callee->text, "write") != 0) return false;
  const Expr *stream = callee->left;
  if (!stream || stream->kind != EXPR_MEMBER || !stream->text) return false;
  if (strcmp(stream->text, "out") != 0 && strcmp(stream->text, "err") != 0) return false;
  const Expr *root = stream->left;
  if (!root || root->kind != EXPR_IDENT || !root->text) return false;
  const char *root_type = scope_type(scope, root->text);
  return root_type && strcmp(root_type, "World") == 0;
}

static bool is_world_stream_write_call(const Expr *expr, Scope *scope) {
  return expr && expr->kind == EXPR_CALL && is_world_stream_write_callee(expr->left, scope);
}

static int std_http_error_code(const char *name) {
  if (!name) return -1;
  if (strcmp(name, "std.http.errorNone") == 0) return 0;
  if (strcmp(name, "std.http.errorInvalidUrl") == 0) return 1;
  if (strcmp(name, "std.http.errorUnsupportedProtocol") == 0) return 2;
  if (strcmp(name, "std.http.errorDns") == 0) return 3;
  if (strcmp(name, "std.http.errorConnect") == 0) return 4;
  if (strcmp(name, "std.http.errorTls") == 0) return 5;
  if (strcmp(name, "std.http.errorTimeout") == 0) return 6;
  if (strcmp(name, "std.http.errorTooLarge") == 0) return 7;
  if (strcmp(name, "std.http.errorProviderUnavailable") == 0) return 8;
  if (strcmp(name, "std.http.errorIo") == 0) return 9;
  if (strcmp(name, "std.http.errorInvalidRequest") == 0) return 10;
  return -1;
}

static const char *std_call_return_type(const Expr *callee) {
  ZBuf name;
  zbuf_init(&name);
  member_name_buf(callee, &name);
  const char *result = "Unknown";
  if (strcmp(name.data, "std.codec.crc32") == 0) result = "u32";
  else if (strcmp(name.data, "std.codec.crc32Bytes") == 0) result = "u32";
  else if (strcmp(name.data, "std.codec.encodedVarintLen") == 0) result = "usize";
  else if (strcmp(name.data, "std.codec.readU8") == 0) result = "u8";
  else if (strcmp(name.data, "std.codec.readU16") == 0) result = "u16";
  else if (strcmp(name.data, "std.codec.readU32") == 0) result = "u32";
  else if (strcmp(name.data, "std.codec.writeU16") == 0) result = "u32";
  else if (strcmp(name.data, "std.codec.writeU32") == 0) result = "u32";
  else if (strcmp(name.data, "std.codec.base64EncodedLen") == 0) result = "usize";
  else if (strcmp(name.data, "std.codec.base64Encode") == 0) result = "Maybe<String>";
  else if (strcmp(name.data, "std.codec.hexEncode") == 0) result = "Maybe<String>";
  else if (strcmp(name.data, "std.codec.utf8Valid") == 0) result = "Bool";
  else if (strcmp(name.data, "std.codec.urlEncode") == 0) result = "Maybe<String>";
  else if (strcmp(name.data, "std.mem.copy") == 0) result = "usize";
  else if (strcmp(name.data, "std.mem.fill") == 0) result = "usize";
  else if (strcmp(name.data, "std.mem.eql") == 0) result = "Bool";
  else if (strcmp(name.data, "std.mem.span") == 0) result = "Span<u8>";
  else if (strcmp(name.data, "std.mem.len") == 0) result = "usize";
  else if (strcmp(name.data, "std.mem.get") == 0) result = "Unknown";
  else if (strcmp(name.data, "std.mem.eqlBytes") == 0) result = "Bool";
  else if (strcmp(name.data, "std.mem.nullAlloc") == 0) result = "NullAlloc";
  else if (strcmp(name.data, "std.mem.fixedBufAlloc") == 0) result = "FixedBufAlloc";
  else if (strcmp(name.data, "std.mem.arena") == 0) result = "FixedBufAlloc";
  else if (strcmp(name.data, "std.mem.pageAlloc") == 0) result = "PageAlloc";
  else if (strcmp(name.data, "std.mem.generalAlloc") == 0) result = "GeneralAlloc";
  else if (strcmp(name.data, "std.mem.allocBytes") == 0) result = "Maybe<MutSpan<u8>>";
  else if (strcmp(name.data, "std.mem.byteBuf") == 0) result = "Maybe<owned<ByteBuf>>";
  else if (strcmp(name.data, "std.mem.vec") == 0) result = "Vec";
  else if (strcmp(name.data, "std.mem.vecPush") == 0) result = "Bool";
  else if (strcmp(name.data, "std.mem.vecLen") == 0) result = "usize";
  else if (strcmp(name.data, "std.mem.vecCapacity") == 0) result = "usize";
  else if (strcmp(name.data, "std.mem.mapEmpty") == 0) result = "Map";
  else if (strcmp(name.data, "std.mem.mapLen") == 0) result = "usize";
  else if (strcmp(name.data, "std.mem.setEmpty") == 0) result = "Set";
  else if (strcmp(name.data, "std.mem.setLen") == 0) result = "usize";
  else if (strcmp(name.data, "std.mem.bufBytes") == 0) result = "MutSpan<u8>";
  else if (strcmp(name.data, "std.mem.bufLen") == 0) result = "usize";
  else if (strcmp(name.data, "std.mem.reset") == 0) result = "Void";
  else if (strcmp(name.data, "std.mem.capacity") == 0) result = "usize";
  else if (strcmp(name.data, "std.path.basename") == 0) result = "String";
  else if (strcmp(name.data, "std.path.dirname") == 0) result = "String";
  else if (strcmp(name.data, "std.path.extension") == 0) result = "String";
  else if (strcmp(name.data, "std.path.join") == 0) result = "Maybe<String>";
  else if (strcmp(name.data, "std.path.normalize") == 0) result = "Maybe<String>";
  else if (strcmp(name.data, "std.path.relative") == 0) result = "Maybe<String>";
  else if (strcmp(name.data, "std.io.bufferedReader") == 0) result = "BufferedReader";
  else if (strcmp(name.data, "std.io.bufferedWriter") == 0) result = "BufferedWriter";
  else if (strcmp(name.data, "std.io.readerCapacity") == 0) result = "usize";
  else if (strcmp(name.data, "std.io.writerCapacity") == 0) result = "usize";
  else if (strcmp(name.data, "std.io.copy") == 0) result = "usize";
  else if (strcmp(name.data, "std.parse.isAsciiDigit") == 0) result = "Bool";
  else if (strcmp(name.data, "std.parse.isAsciiAlpha") == 0) result = "Bool";
  else if (strcmp(name.data, "std.parse.isIdentifierStart") == 0) result = "Bool";
  else if (strcmp(name.data, "std.parse.isWhitespace") == 0) result = "Bool";
  else if (strcmp(name.data, "std.parse.scanDigits") == 0) result = "usize";
  else if (strcmp(name.data, "std.parse.scanIdentifier") == 0) result = "usize";
  else if (strcmp(name.data, "std.parse.parseU8") == 0) result = "Maybe<u8>";
  else if (strcmp(name.data, "std.parse.parseU16") == 0) result = "Maybe<u16>";
  else if (strcmp(name.data, "std.parse.parseU32") == 0) result = "Maybe<u32>";
  else if (strcmp(name.data, "std.json.validate") == 0) result = "Bool";
  else if (strcmp(name.data, "std.json.validateBytes") == 0) result = "Bool";
  else if (strcmp(name.data, "std.json.parse") == 0) result = "Maybe<JsonDoc>";
  else if (strcmp(name.data, "std.json.parseBytes") == 0) result = "Maybe<JsonDoc>";
  else if (strcmp(name.data, "std.json.streamTokens") == 0) result = "usize";
  else if (strcmp(name.data, "std.json.streamTokensBytes") == 0) result = "usize";
  else if (strcmp(name.data, "std.json.writeString") == 0) result = "Maybe<String>";
  else if (strcmp(name.data, "std.json.decodeBoundary") == 0) result = "String";
  else if (strcmp(name.data, "std.time.ms") == 0) result = "Duration";
  else if (strcmp(name.data, "std.time.seconds") == 0) result = "Duration";
  else if (strcmp(name.data, "std.time.add") == 0) result = "Duration";
  else if (strcmp(name.data, "std.time.sub") == 0) result = "Duration";
  else if (strcmp(name.data, "std.time.min") == 0) result = "Duration";
  else if (strcmp(name.data, "std.time.max") == 0) result = "Duration";
  else if (strcmp(name.data, "std.time.monotonic") == 0) result = "Duration";
  else if (strcmp(name.data, "std.time.wallSeconds") == 0) result = "i64";
  else if (strcmp(name.data, "std.time.asMsFloor") == 0) result = "i32";
  else if (strcmp(name.data, "std.time.lessThan") == 0) result = "Bool";
  else if (strcmp(name.data, "std.rand.seed") == 0) result = "RandSource";
  else if (strcmp(name.data, "std.rand.nextU32") == 0) result = "u32";
  else if (strcmp(name.data, "std.rand.entropyU32") == 0) result = "u32";
  else if (strcmp(name.data, "std.proc.spawn") == 0) result = "ProcStatus";
  else if (strcmp(name.data, "std.proc.exitCode") == 0) result = "i32";
  else if (strcmp(name.data, "std.crypto.hash32") == 0) result = "u32";
  else if (strcmp(name.data, "std.crypto.hmac32") == 0) result = "u32";
  else if (strcmp(name.data, "std.crypto.constantTimeEql") == 0) result = "Bool";
  else if (strcmp(name.data, "std.crypto.secureRandomU32") == 0) result = "u32";
  else if (strcmp(name.data, "std.net.address") == 0) result = "Address";
  else if (strcmp(name.data, "std.net.host") == 0) result = "Net";
  else if (strcmp(name.data, "std.net.dnsName") == 0) result = "String";
  else if (strcmp(name.data, "std.net.connect") == 0) result = "Maybe<Conn>";
  else if (strcmp(name.data, "std.net.listen") == 0) result = "Maybe<Listener>";
  else if (strcmp(name.data, "std.net.withTimeout") == 0) result = "Address";
  else if (strcmp(name.data, "std.http.parseMethod") == 0) result = "HttpMethod";
  else if (strcmp(name.data, "std.http.client") == 0) result = "HttpClient";
  else if (strcmp(name.data, "std.http.server") == 0) result = "HttpServer";
  else if (strcmp(name.data, "std.http.fetch") == 0) result = "HttpResult";
  else if (strcmp(name.data, "std.http.resultOk") == 0) result = "Bool";
  else if (strcmp(name.data, "std.http.resultStatus") == 0) result = "u16";
  else if (strcmp(name.data, "std.http.resultBodyLen") == 0) result = "usize";
  else if (strcmp(name.data, "std.http.resultError") == 0) result = "HttpError";
  else if (std_http_error_code(name.data) >= 0) result = "HttpError";
  else if (strcmp(name.data, "std.http.responseLen") == 0) result = "usize";
  else if (strcmp(name.data, "std.http.responseHeadersLen") == 0) result = "usize";
  else if (strcmp(name.data, "std.http.responseBodyOffset") == 0) result = "usize";
  else if (strcmp(name.data, "std.http.headerValue") == 0) result = "HttpHeaderValue";
  else if (strcmp(name.data, "std.http.headerFound") == 0) result = "Bool";
  else if (strcmp(name.data, "std.http.headerOffset") == 0) result = "usize";
  else if (strcmp(name.data, "std.http.headerLen") == 0) result = "usize";
  else if (strcmp(name.data, "std.http.tlsBoundary") == 0) result = "String";
  else if (strcmp(name.data, "std.args.len") == 0) result = "usize";
  else if (strcmp(name.data, "std.args.get") == 0) result = "Maybe<String>";
  else if (strcmp(name.data, "std.env.get") == 0) result = "Maybe<String>";
  else if (strcmp(name.data, "std.fs.host") == 0) result = "Fs";
  else if (strcmp(name.data, "std.fs.open") == 0) result = "Maybe<owned<File>>";
  else if (strcmp(name.data, "std.fs.openOrRaise") == 0) result = "owned<File>";
  else if (strcmp(name.data, "std.fs.create") == 0) result = "Maybe<owned<File>>";
  else if (strcmp(name.data, "std.fs.createOrRaise") == 0) result = "owned<File>";
  else if (strcmp(name.data, "std.fs.read") == 0) result = "usize";
  else if (strcmp(name.data, "std.fs.readOrRaise") == 0) result = "usize";
  else if (strcmp(name.data, "std.fs.write") == 0) result = "usize";
  else if (strcmp(name.data, "std.fs.writeAll") == 0) result = "Bool";
  else if (strcmp(name.data, "std.fs.writeAllOrRaise") == 0) result = "Void";
  else if (strcmp(name.data, "std.fs.close") == 0) result = "Void";
  else if (strcmp(name.data, "std.fs.readAll") == 0) result = "Maybe<owned<ByteBuf>>";
  else if (strcmp(name.data, "std.fs.readAllOrRaise") == 0) result = "owned<ByteBuf>";
  else if (strcmp(name.data, "std.fs.readBytes") == 0) result = "Maybe<usize>";
  else if (strcmp(name.data, "std.fs.writeBytes") == 0) result = "Maybe<usize>";
  else if (strcmp(name.data, "std.fs.exists") == 0) result = "Bool";
  else if (strcmp(name.data, "std.fs.isDir") == 0) result = "Bool";
  else if (strcmp(name.data, "std.fs.makeDir") == 0) result = "Bool";
  else if (strcmp(name.data, "std.fs.removeDir") == 0) result = "Bool";
  else if (strcmp(name.data, "std.fs.remove") == 0) result = "Bool";
  else if (strcmp(name.data, "std.fs.rename") == 0) result = "Bool";
  else if (strcmp(name.data, "std.fs.dirEntryCount") == 0) result = "Maybe<usize>";
  else if (strcmp(name.data, "std.fs.tempName") == 0) result = "Maybe<String>";
  else if (strcmp(name.data, "std.fs.atomicWrite") == 0) result = "Bool";
  else if (strcmp(name.data, "std.fs.fileLen") == 0) result = "Maybe<usize>";
  else if (strcmp(name.data, "std.fs.fileLenOrRaise") == 0) result = "usize";
  zbuf_free(&name);
  return result;
}

static int std_call_arg_count(const char *name) {
  if (strcmp(name, "std.codec.crc32") == 0) return 1;
  if (strcmp(name, "std.codec.crc32Bytes") == 0) return 1;
  if (strcmp(name, "std.codec.encodedVarintLen") == 0) return 1;
  if (strcmp(name, "std.codec.readU8") == 0) return 1;
  if (strcmp(name, "std.codec.readU16") == 0) return 1;
  if (strcmp(name, "std.codec.readU32") == 0) return 1;
  if (strcmp(name, "std.codec.writeU16") == 0) return 1;
  if (strcmp(name, "std.codec.writeU32") == 0) return 1;
  if (strcmp(name, "std.codec.base64EncodedLen") == 0) return 1;
  if (strcmp(name, "std.codec.base64Encode") == 0) return 2;
  if (strcmp(name, "std.codec.hexEncode") == 0) return 2;
  if (strcmp(name, "std.codec.utf8Valid") == 0) return 1;
  if (strcmp(name, "std.codec.urlEncode") == 0) return 2;
  if (strcmp(name, "std.mem.copy") == 0) return 2;
  if (strcmp(name, "std.mem.fill") == 0) return 2;
  if (strcmp(name, "std.mem.eql") == 0) return 2;
  if (strcmp(name, "std.mem.span") == 0) return 1;
  if (strcmp(name, "std.mem.len") == 0) return 1;
  if (strcmp(name, "std.mem.get") == 0) return 2;
  if (strcmp(name, "std.mem.eqlBytes") == 0) return 2;
  if (strcmp(name, "std.mem.nullAlloc") == 0) return 0;
  if (strcmp(name, "std.mem.fixedBufAlloc") == 0) return 1;
  if (strcmp(name, "std.mem.arena") == 0) return 1;
  if (strcmp(name, "std.mem.pageAlloc") == 0) return 0;
  if (strcmp(name, "std.mem.generalAlloc") == 0) return 0;
  if (strcmp(name, "std.mem.allocBytes") == 0) return 2;
  if (strcmp(name, "std.mem.byteBuf") == 0) return 2;
  if (strcmp(name, "std.mem.vec") == 0) return 1;
  if (strcmp(name, "std.mem.vecPush") == 0) return 2;
  if (strcmp(name, "std.mem.vecLen") == 0) return 1;
  if (strcmp(name, "std.mem.vecCapacity") == 0) return 1;
  if (strcmp(name, "std.mem.mapEmpty") == 0) return 0;
  if (strcmp(name, "std.mem.mapLen") == 0) return 1;
  if (strcmp(name, "std.mem.setEmpty") == 0) return 0;
  if (strcmp(name, "std.mem.setLen") == 0) return 1;
  if (strcmp(name, "std.mem.bufBytes") == 0) return 1;
  if (strcmp(name, "std.mem.bufLen") == 0) return 1;
  if (strcmp(name, "std.mem.reset") == 0) return 1;
  if (strcmp(name, "std.mem.capacity") == 0) return 1;
  if (strcmp(name, "std.path.basename") == 0) return 1;
  if (strcmp(name, "std.path.dirname") == 0) return 1;
  if (strcmp(name, "std.path.extension") == 0) return 1;
  if (strcmp(name, "std.path.join") == 0) return 3;
  if (strcmp(name, "std.path.normalize") == 0) return 2;
  if (strcmp(name, "std.path.relative") == 0) return 3;
  if (strcmp(name, "std.io.bufferedReader") == 0) return 1;
  if (strcmp(name, "std.io.bufferedWriter") == 0) return 1;
  if (strcmp(name, "std.io.readerCapacity") == 0) return 1;
  if (strcmp(name, "std.io.writerCapacity") == 0) return 1;
  if (strcmp(name, "std.io.copy") == 0) return 2;
  if (strcmp(name, "std.parse.isAsciiDigit") == 0) return 1;
  if (strcmp(name, "std.parse.isAsciiAlpha") == 0) return 1;
  if (strcmp(name, "std.parse.isIdentifierStart") == 0) return 1;
  if (strcmp(name, "std.parse.isWhitespace") == 0) return 1;
  if (strcmp(name, "std.parse.scanDigits") == 0) return 1;
  if (strcmp(name, "std.parse.scanIdentifier") == 0) return 1;
  if (strcmp(name, "std.parse.parseU8") == 0) return 1;
  if (strcmp(name, "std.parse.parseU16") == 0) return 1;
  if (strcmp(name, "std.parse.parseU32") == 0) return 1;
  if (strcmp(name, "std.json.validate") == 0) return 1;
  if (strcmp(name, "std.json.validateBytes") == 0) return 1;
  if (strcmp(name, "std.json.parse") == 0) return 2;
  if (strcmp(name, "std.json.parseBytes") == 0) return 2;
  if (strcmp(name, "std.json.streamTokens") == 0) return 1;
  if (strcmp(name, "std.json.streamTokensBytes") == 0) return 1;
  if (strcmp(name, "std.json.writeString") == 0) return 2;
  if (strcmp(name, "std.json.decodeBoundary") == 0) return 0;
  if (strcmp(name, "std.time.ms") == 0) return 1;
  if (strcmp(name, "std.time.seconds") == 0) return 1;
  if (strcmp(name, "std.time.add") == 0) return 2;
  if (strcmp(name, "std.time.sub") == 0) return 2;
  if (strcmp(name, "std.time.asMsFloor") == 0) return 1;
  if (strcmp(name, "std.time.min") == 0) return 2;
  if (strcmp(name, "std.time.max") == 0) return 2;
  if (strcmp(name, "std.time.lessThan") == 0) return 2;
  if (strcmp(name, "std.time.monotonic") == 0) return 0;
  if (strcmp(name, "std.time.wallSeconds") == 0) return 0;
  if (strcmp(name, "std.rand.seed") == 0) return 1;
  if (strcmp(name, "std.rand.nextU32") == 0) return 1;
  if (strcmp(name, "std.rand.entropyU32") == 0) return 0;
  if (strcmp(name, "std.proc.spawn") == 0) return 1;
  if (strcmp(name, "std.proc.exitCode") == 0) return 1;
  if (strcmp(name, "std.crypto.hash32") == 0) return 1;
  if (strcmp(name, "std.crypto.hmac32") == 0) return 2;
  if (strcmp(name, "std.crypto.constantTimeEql") == 0) return 2;
  if (strcmp(name, "std.crypto.secureRandomU32") == 0) return 0;
  if (strcmp(name, "std.net.address") == 0) return 2;
  if (strcmp(name, "std.net.host") == 0) return 0;
  if (strcmp(name, "std.net.dnsName") == 0) return 1;
  if (strcmp(name, "std.net.connect") == 0) return 2;
  if (strcmp(name, "std.net.listen") == 0) return 2;
  if (strcmp(name, "std.net.withTimeout") == 0) return 2;
  if (strcmp(name, "std.http.parseMethod") == 0) return 1;
  if (strcmp(name, "std.http.client") == 0) return 1;
  if (strcmp(name, "std.http.server") == 0) return 2;
  if (strcmp(name, "std.http.fetch") == 0) return 4;
  if (strcmp(name, "std.http.resultOk") == 0) return 1;
  if (strcmp(name, "std.http.resultStatus") == 0) return 1;
  if (strcmp(name, "std.http.resultBodyLen") == 0) return 1;
  if (strcmp(name, "std.http.resultError") == 0) return 1;
  if (std_http_error_code(name) >= 0) return 0;
  if (strcmp(name, "std.http.responseLen") == 0) return 1;
  if (strcmp(name, "std.http.responseHeadersLen") == 0) return 1;
  if (strcmp(name, "std.http.responseBodyOffset") == 0) return 1;
  if (strcmp(name, "std.http.headerValue") == 0) return 2;
  if (strcmp(name, "std.http.headerFound") == 0) return 1;
  if (strcmp(name, "std.http.headerOffset") == 0) return 1;
  if (strcmp(name, "std.http.headerLen") == 0) return 1;
  if (strcmp(name, "std.http.tlsBoundary") == 0) return 0;
  if (strcmp(name, "std.args.len") == 0) return 0;
  if (strcmp(name, "std.args.get") == 0) return 1;
  if (strcmp(name, "std.env.get") == 0) return 1;
  if (strcmp(name, "std.fs.host") == 0) return 0;
  if (strcmp(name, "std.fs.open") == 0) return 2;
  if (strcmp(name, "std.fs.openOrRaise") == 0) return 2;
  if (strcmp(name, "std.fs.create") == 0) return 2;
  if (strcmp(name, "std.fs.createOrRaise") == 0) return 2;
  if (strcmp(name, "std.fs.read") == 0) return 2;
  if (strcmp(name, "std.fs.readOrRaise") == 0) return 2;
  if (strcmp(name, "std.fs.write") == 0) return 2;
  if (strcmp(name, "std.fs.writeAll") == 0) return 2;
  if (strcmp(name, "std.fs.writeAllOrRaise") == 0) return 2;
  if (strcmp(name, "std.fs.close") == 0) return 1;
  if (strcmp(name, "std.fs.readAll") == 0) return 4;
  if (strcmp(name, "std.fs.readAllOrRaise") == 0) return 4;
  if (strcmp(name, "std.fs.readBytes") == 0) return 2;
  if (strcmp(name, "std.fs.writeBytes") == 0) return 2;
  if (strcmp(name, "std.fs.exists") == 0) return 1;
  if (strcmp(name, "std.fs.isDir") == 0) return 1;
  if (strcmp(name, "std.fs.makeDir") == 0) return 1;
  if (strcmp(name, "std.fs.removeDir") == 0) return 1;
  if (strcmp(name, "std.fs.remove") == 0) return 1;
  if (strcmp(name, "std.fs.rename") == 0) return 2;
  if (strcmp(name, "std.fs.dirEntryCount") == 0) return 1;
  if (strcmp(name, "std.fs.tempName") == 0) return 2;
  if (strcmp(name, "std.fs.atomicWrite") == 0) return 3;
  if (strcmp(name, "std.fs.fileLen") == 0) return 1;
  if (strcmp(name, "std.fs.fileLenOrRaise") == 0) return 1;
  return -1;
}

static const char *std_call_arg_type(const char *name, size_t index) {
  if (strcmp(name, "std.codec.crc32") == 0) return "String";
  if (strcmp(name, "std.codec.crc32Bytes") == 0) return "Span<u8>";
  if (strcmp(name, "std.codec.encodedVarintLen") == 0) return "u32";
  if (strcmp(name, "std.codec.readU8") == 0) return "String";
  if (strcmp(name, "std.codec.readU16") == 0) return "String";
  if (strcmp(name, "std.codec.readU32") == 0) return "String";
  if (strcmp(name, "std.codec.writeU16") == 0) return "u32";
  if (strcmp(name, "std.codec.writeU32") == 0) return "u32";
  if (strcmp(name, "std.codec.base64EncodedLen") == 0) return "usize";
  if (strcmp(name, "std.codec.base64Encode") == 0) return index == 0 ? "MutSpan<u8>" : "Span<u8>";
  if (strcmp(name, "std.codec.hexEncode") == 0) return index == 0 ? "MutSpan<u8>" : "Span<u8>";
  if (strcmp(name, "std.codec.utf8Valid") == 0) return "Span<u8>";
  if (strcmp(name, "std.codec.urlEncode") == 0) return index == 0 ? "MutSpan<u8>" : "String";
  if (strcmp(name, "std.mem.copy") == 0) return index == 0 ? "MutSpan<u8>" : "Span<u8>";
  if (strcmp(name, "std.mem.fill") == 0) return index == 0 ? "MutSpan<u8>" : "u8";
  if (strcmp(name, "std.mem.eql") == 0) return "String";
  if (strcmp(name, "std.mem.span") == 0) return "String";
  if (strcmp(name, "std.mem.get") == 0) return index == 1 ? "usize" : NULL;
  if (strcmp(name, "std.mem.fixedBufAlloc") == 0) return "MutSpan<u8>";
  if (strcmp(name, "std.mem.arena") == 0) return "MutSpan<u8>";
  if (strcmp(name, "std.mem.allocBytes") == 0) return index == 1 ? "usize" : NULL;
  if (strcmp(name, "std.mem.byteBuf") == 0) return index == 1 ? "usize" : NULL;
  if (strcmp(name, "std.mem.vec") == 0) return "MutSpan<u8>";
  if (strcmp(name, "std.mem.vecPush") == 0) return index == 0 ? "mutref<Vec>" : "u8";
  if (strcmp(name, "std.mem.vecLen") == 0) return "ref<Vec>";
  if (strcmp(name, "std.mem.vecCapacity") == 0) return "ref<Vec>";
  if (strcmp(name, "std.mem.mapLen") == 0) return "ref<Map>";
  if (strcmp(name, "std.mem.setLen") == 0) return "ref<Set>";
  if (strcmp(name, "std.mem.bufBytes") == 0) return "ref<ByteBuf>";
  if (strcmp(name, "std.mem.bufLen") == 0) return "ref<ByteBuf>";
  if (strcmp(name, "std.mem.reset") == 0) return "mutref<FixedBufAlloc>";
  if (strcmp(name, "std.mem.capacity") == 0) return "FixedBufAlloc";
  if (strcmp(name, "std.path.basename") == 0) return "String";
  if (strcmp(name, "std.path.dirname") == 0) return "String";
  if (strcmp(name, "std.path.extension") == 0) return "String";
  if (strcmp(name, "std.path.join") == 0) return index == 0 ? "MutSpan<u8>" : "String";
  if (strcmp(name, "std.path.normalize") == 0) return index == 0 ? "MutSpan<u8>" : "String";
  if (strcmp(name, "std.path.relative") == 0) return index == 0 ? "MutSpan<u8>" : "String";
  if (strcmp(name, "std.io.bufferedReader") == 0) return "MutSpan<u8>";
  if (strcmp(name, "std.io.bufferedWriter") == 0) return "MutSpan<u8>";
  if (strcmp(name, "std.io.readerCapacity") == 0) return "ref<BufferedReader>";
  if (strcmp(name, "std.io.writerCapacity") == 0) return "ref<BufferedWriter>";
  if (strcmp(name, "std.io.copy") == 0) return index == 0 ? "MutSpan<u8>" : "Span<u8>";
  if (strcmp(name, "std.parse.isAsciiDigit") == 0) return "String";
  if (strcmp(name, "std.parse.isAsciiAlpha") == 0) return "String";
  if (strcmp(name, "std.parse.isIdentifierStart") == 0) return "String";
  if (strcmp(name, "std.parse.isWhitespace") == 0) return "String";
  if (strcmp(name, "std.parse.scanDigits") == 0) return "String";
  if (strcmp(name, "std.parse.scanIdentifier") == 0) return "String";
  if (strcmp(name, "std.parse.parseU8") == 0) return "String";
  if (strcmp(name, "std.parse.parseU16") == 0) return "String";
  if (strcmp(name, "std.parse.parseU32") == 0) return "String";
  if (strcmp(name, "std.json.validate") == 0) return "String";
  if (strcmp(name, "std.json.validateBytes") == 0) return "Span<u8>";
  if (strcmp(name, "std.json.parse") == 0) return index == 1 ? "String" : NULL;
  if (strcmp(name, "std.json.parseBytes") == 0) return index == 1 ? "Span<u8>" : NULL;
  if (strcmp(name, "std.json.streamTokens") == 0) return "String";
  if (strcmp(name, "std.json.streamTokensBytes") == 0) return "Span<u8>";
  if (strcmp(name, "std.json.writeString") == 0) return index == 0 ? "MutSpan<u8>" : "String";
  if (strcmp(name, "std.time.ms") == 0) return "i32";
  if (strcmp(name, "std.time.seconds") == 0) return "i32";
  if (strcmp(name, "std.time.add") == 0) return index == 0 ? "Duration" : "Duration";
  if (strcmp(name, "std.time.sub") == 0) return index == 0 ? "Duration" : "Duration";
  if (strcmp(name, "std.time.asMsFloor") == 0) return "Duration";
  if (strcmp(name, "std.time.min") == 0) return "Duration";
  if (strcmp(name, "std.time.max") == 0) return "Duration";
  if (strcmp(name, "std.time.lessThan") == 0) return "Duration";
  if (strcmp(name, "std.rand.seed") == 0) return "u32";
  if (strcmp(name, "std.rand.nextU32") == 0) return "mutref<RandSource>";
  if (strcmp(name, "std.proc.spawn") == 0) return "String";
  if (strcmp(name, "std.proc.exitCode") == 0) return "ProcStatus";
  if (strcmp(name, "std.crypto.hash32") == 0) return "Span<u8>";
  if (strcmp(name, "std.crypto.hmac32") == 0) return "Span<u8>";
  if (strcmp(name, "std.crypto.constantTimeEql") == 0) return index == 0 ? "Span<u8>" : "Span<u8>";
  if (strcmp(name, "std.net.address") == 0) return index == 0 ? "String" : "u16";
  if (strcmp(name, "std.net.dnsName") == 0) return "Address";
  if (strcmp(name, "std.net.connect") == 0) return index == 0 ? "Net" : "Address";
  if (strcmp(name, "std.net.listen") == 0) return index == 0 ? "Net" : "Address";
  if (strcmp(name, "std.net.withTimeout") == 0) return index == 0 ? "Address" : "Duration";
  if (strcmp(name, "std.http.parseMethod") == 0) return "String";
  if (strcmp(name, "std.http.client") == 0) return "Net";
  if (strcmp(name, "std.http.server") == 0) return index == 0 ? "Net" : "Address";
  if (strcmp(name, "std.http.fetch") == 0) {
    if (index == 0) return "HttpClient";
    if (index == 1) return "Span<u8>";
    if (index == 2) return "MutSpan<u8>";
    return "Duration";
  }
  if (strcmp(name, "std.http.resultOk") == 0 ||
      strcmp(name, "std.http.resultStatus") == 0 ||
      strcmp(name, "std.http.resultBodyLen") == 0 ||
      strcmp(name, "std.http.resultError") == 0) return "HttpResult";
  if (strcmp(name, "std.http.responseLen") == 0 ||
      strcmp(name, "std.http.responseHeadersLen") == 0 ||
      strcmp(name, "std.http.responseBodyOffset") == 0) return "Span<u8>";
  if (strcmp(name, "std.http.headerValue") == 0) return "Span<u8>";
  if (strcmp(name, "std.http.headerFound") == 0 ||
      strcmp(name, "std.http.headerOffset") == 0 ||
      strcmp(name, "std.http.headerLen") == 0) return "HttpHeaderValue";
  if (strcmp(name, "std.args.get") == 0) return "usize";
  if (strcmp(name, "std.env.get") == 0) return "String";
  if (strcmp(name, "std.fs.open") == 0) return index == 0 ? "Fs" : "String";
  if (strcmp(name, "std.fs.openOrRaise") == 0) return index == 0 ? "Fs" : "String";
  if (strcmp(name, "std.fs.create") == 0) return index == 0 ? "Fs" : "String";
  if (strcmp(name, "std.fs.createOrRaise") == 0) return index == 0 ? "Fs" : "String";
  if (strcmp(name, "std.fs.read") == 0) return index == 0 ? "String" : "MutSpan<u8>";
  if (strcmp(name, "std.fs.readOrRaise") == 0) return index == 0 ? "mutref<File>" : "MutSpan<u8>";
  if (strcmp(name, "std.fs.write") == 0) return "String";
  if (strcmp(name, "std.fs.writeAll") == 0) return index == 0 ? "mutref<File>" : "Span<u8>";
  if (strcmp(name, "std.fs.writeAllOrRaise") == 0) return index == 0 ? "mutref<File>" : "Span<u8>";
  if (strcmp(name, "std.fs.close") == 0) return "mutref<File>";
  if (strcmp(name, "std.fs.readAll") == 0) {
    if (index == 1) return "Fs";
    if (index == 2) return "String";
    if (index == 3) return "usize";
    return NULL;
  }
  if (strcmp(name, "std.fs.readAllOrRaise") == 0) {
    if (index == 1) return "Fs";
    if (index == 2) return "String";
    if (index == 3) return "usize";
    return NULL;
  }
  if (strcmp(name, "std.fs.readBytes") == 0) return index == 0 ? "String" : "MutSpan<u8>";
  if (strcmp(name, "std.fs.writeBytes") == 0) return index == 0 ? "String" : "Span<u8>";
  if (strcmp(name, "std.fs.exists") == 0) return "String";
  if (strcmp(name, "std.fs.isDir") == 0) return "String";
  if (strcmp(name, "std.fs.makeDir") == 0) return "String";
  if (strcmp(name, "std.fs.removeDir") == 0) return "String";
  if (strcmp(name, "std.fs.remove") == 0) return "String";
  if (strcmp(name, "std.fs.rename") == 0) return "String";
  if (strcmp(name, "std.fs.dirEntryCount") == 0) return "String";
  if (strcmp(name, "std.fs.tempName") == 0) return index == 0 ? "MutSpan<u8>" : "String";
  if (strcmp(name, "std.fs.atomicWrite") == 0) return index == 2 ? "Span<u8>" : "String";
  if (strcmp(name, "std.fs.fileLen") == 0) return "mutref<File>";
  if (strcmp(name, "std.fs.fileLenOrRaise") == 0) return "mutref<File>";
  return NULL;
}

static bool type_has_generic_arg(const char *type, const char *name, const char **inner, size_t *inner_len) {
  if (!type || !name) return false;
  size_t name_len = strlen(name);
  size_t type_len = strlen(type);
  if (type_len <= name_len + 2 || strncmp(type, name, name_len) != 0 || type[name_len] != '<' || type[type_len - 1] != '>') return false;
  int depth = 0;
  for (size_t i = name_len; i < type_len; i++) {
    if (type[i] == '<') depth++;
    else if (type[i] == '>') {
      depth--;
      if (depth == 0 && i != type_len - 1) return false;
      if (depth < 0) return false;
    } else if (type[i] == ',' && depth == 1) {
      return false;
    }
  }
  if (depth != 0) return false;
  *inner = type + name_len + 1;
  *inner_len = type_len - name_len - 2;
  return *inner_len > 0;
}

static bool split_generic_args(const char *inner, size_t inner_len, char ***out_items, size_t *out_len) {
  char **items = NULL;
  size_t len = 0;
  size_t cap = 0;
  size_t start = 0;
  int depth = 0;
  for (size_t i = 0; i <= inner_len; i++) {
    char c = i < inner_len ? inner[i] : ',';
    if (c == '<') depth++;
    else if (c == '>') depth--;
    if ((c == ',' && depth == 0) || i == inner_len) {
      size_t end = i;
      while (start < end && isspace((unsigned char)inner[start])) start++;
      while (end > start && isspace((unsigned char)inner[end - 1])) end--;
      if (end == start) {
        for (size_t j = 0; j < len; j++) free(items[j]);
        free(items);
        return false;
      }
      items = checker_grow_items(items, len, &cap, 4, sizeof(char *));
      items[len++] = z_strndup(inner + start, end - start);
      start = i + 1;
    }
    if (depth < 0) {
      for (size_t j = 0; j < len; j++) free(items[j]);
      free(items);
      return false;
    }
  }
  if (depth != 0 || len == 0) {
    for (size_t j = 0; j < len; j++) free(items[j]);
    free(items);
    return false;
  }
  *out_items = items;
  *out_len = len;
  return true;
}

static bool type_generic_arg_list(const char *type, const char *name, char ***out_items, size_t *out_len) {
  if (!type || !name) return false;
  size_t name_len = strlen(name);
  size_t type_len = strlen(type);
  if (type_len <= name_len + 2 || strncmp(type, name, name_len) != 0 || type[name_len] != '<' || type[type_len - 1] != '>') return false;
  const char *inner = type + name_len + 1;
  size_t inner_len = type_len - name_len - 2;
  return split_generic_args(inner, inner_len, out_items, out_len);
}

static void free_type_arg_list(char **items, size_t len) {
  for (size_t i = 0; i < len; i++) free(items[i]);
  free(items);
}

static bool type_is_owned(const char *type) {
  const char *inner = NULL;
  size_t inner_len = 0;
  return type_has_generic_arg(type, "owned", &inner, &inner_len);
}

static bool is_allocator_type(const char *type) {
  return type && (strcmp(type, "NullAlloc") == 0 || strcmp(type, "FixedBufAlloc") == 0 || strcmp(type, "PageAlloc") == 0 || strcmp(type, "GeneralAlloc") == 0);
}

static bool owned_inner_text(const char *type, char *out, size_t out_len) {
  if (!type || !out || out_len == 0) return false;
  const char *inner = NULL;
  size_t inner_len = 0;
  if (!type_has_generic_arg(type, "owned", &inner, &inner_len)) return false;
  snprintf(out, out_len, "%.*s", (int)inner_len, inner);
  return true;
}

static bool ref_inner_text(const char *type, char *out, size_t out_len) {
  if (!type || !out || out_len == 0) return false;
  const char *inner = NULL;
  size_t inner_len = 0;
  if (!type_has_generic_arg(type, "ref", &inner, &inner_len) &&
      !type_has_generic_arg(type, "mutref", &inner, &inner_len)) return false;
  snprintf(out, out_len, "%.*s", (int)inner_len, inner);
  return true;
}

static bool named_ref_inner_text(const char *type, const char *name, char *out, size_t out_len) {
  if (!type || !name || !out || out_len == 0) return false;
  const char *inner = NULL;
  size_t inner_len = 0;
  if (!type_has_generic_arg(type, name, &inner, &inner_len)) return false;
  snprintf(out, out_len, "%.*s", (int)inner_len, inner);
  return true;
}

static bool expr_root_ident(const Expr *expr, char *out, size_t out_len) {
  if (!expr || !out || out_len == 0) return false;
  switch (expr->kind) {
    case EXPR_IDENT:
      snprintf(out, out_len, "%s", expr->text ? expr->text : "");
      return expr->text != NULL;
    case EXPR_MEMBER:
    case EXPR_INDEX:
      return expr_root_ident(expr->left, out, out_len);
    default:
      return false;
  }
}

static bool expr_static_index_segment(const Expr *expr, char *out, size_t out_len) {
  if (!expr || expr->kind != EXPR_NUMBER || !expr->text || !out || out_len < 4) return false;
  size_t len = strlen(expr->text);
  if (len == 0 || len + 3 > out_len) return false;
  for (size_t i = 0; i < len; i++) {
    if (expr->text[i] < '0' || expr->text[i] > '9') return false;
  }
  snprintf(out, out_len, "[%s]", expr->text);
  return true;
}

static bool expr_binding_path(const Expr *expr, char *root, size_t root_len, char *path, size_t path_len) {
  if (!expr || !root || root_len == 0 || !path || path_len == 0) return false;
  if (expr->kind == EXPR_IDENT) {
    snprintf(root, root_len, "%s", expr->text ? expr->text : "");
    path[0] = '\0';
    return root[0] != '\0';
  }
  if (expr->kind == EXPR_MEMBER) {
    if (!expr_binding_path(expr->left, root, root_len, path, path_len)) return false;
    if (!expr->text || !expr->text[0]) return false;
    size_t current_len = strlen(path);
    size_t field_len = strlen(expr->text);
    size_t needed = current_len ? current_len + 1 + field_len : field_len;
    if (needed + 1 > path_len) return false;
    if (path[0]) {
      snprintf(path + current_len, path_len - current_len, ".%s", expr->text);
    } else {
      snprintf(path, path_len, "%s", expr->text);
    }
    return true;
  }
  if (expr->kind == EXPR_INDEX) {
    if (!expr_binding_path(expr->left, root, root_len, path, path_len)) return false;
    size_t current_len = strlen(path);
    char index_segment[64];
    if (!expr_static_index_segment(expr->right, index_segment, sizeof(index_segment))) {
      snprintf(index_segment, sizeof(index_segment), "[*]");
    }
    size_t needed = current_len + strlen(index_segment);
    if (needed + 1 > path_len) return false;
    snprintf(path + current_len, path_len - current_len, "%s", index_segment);
    return true;
  }
  return false;
}

static bool expr_is_addressable(const Expr *expr) {
  return expr && (expr->kind == EXPR_IDENT || expr->kind == EXPR_MEMBER || expr->kind == EXPR_INDEX);
}

static bool type_is_named_generic(const char *type, const char *name);
static bool expr_reference_provenance(const Program *program, const Expr *expr, Scope *scope, ValueProvenance *origins);
static bool check_expr_expected(const Program *program, const Expr *expr, Scope *scope, ZDiag *diag, const char *expected);
static const char *expr_type(const Program *program, const Expr *expr, Scope *scope);
static bool is_int_type(const char *type);
static void set_expr_resolved_type(const Expr *expr, const char *type);
static bool place_storage_value_provenance_under_path(const Program *program, Scope *scope, const Place *place, const char *relative_path, ValueProvenance *out);
static bool actual_storage_value_provenance_under_path(const Program *program, const Expr *actual, Scope *scope, const char *relative_path, ValueProvenance *out);

static bool borrow_expr_source_is_local_storage(const Expr *borrowed, Scope *scope, const char *root) {
  if (!scope_is_param(scope, root)) return true;
  const char *root_type = scope_type(scope, root);
  if (!type_is_named_generic(root_type, "ref") && !type_is_named_generic(root_type, "mutref")) return true;
  return borrowed && borrowed->kind == EXPR_IDENT;
}

static bool reference_source_origin_is_local_storage(Scope *scope, const char *root) {
  if (!scope_is_param(scope, root)) return true;
  const char *root_type = scope_type(scope, root);
  return !type_is_named_generic(root_type, "ref") && !type_is_named_generic(root_type, "mutref");
}

static bool reference_place_origin_is_local_storage(Scope *scope, const char *root) {
  return !scope_is_param(scope, root);
}

static bool expr_value_provenance(const Expr *expr, Scope *scope, ValueProvenance *origins) {
  if (!expr || !origins) return false;
  if (expr->kind == EXPR_BORROW) {
    char root[128];
    char path[256];
    if (!expr_binding_path(expr->left, root, sizeof(root), path, sizeof(path))) return false;
    const char *root_type = scope_type(scope, root);
    if (path[0] && root_type && (type_is_named_generic(root_type, "ref") || type_is_named_generic(root_type, "mutref"))) {
      ValueProvenance binding_origins = {0};
      bool added = false;
      if (scope_copy_value_provenance(scope, root, &binding_origins)) {
        added = value_provenance_add_direct_as_with_origin_suffix(origins, &binding_origins, expr->mutable_borrow, path);
      }
      value_provenance_free(&binding_origins);
      if (added) return true;
    }
    return value_provenance_add_full(origins, root, scope_binding_scope(scope, root), expr->mutable_borrow, borrow_expr_source_is_local_storage(expr->left, scope, root), NULL, path);
  }
  if (expr->kind == EXPR_IDENT) {
    if (scope_copy_value_provenance(scope, expr->text, origins)) return true;
    const char *actual = scope_type(scope, expr->text);
    if (actual && (type_is_named_generic(actual, "ref") || type_is_named_generic(actual, "mutref"))) {
      return value_provenance_add(origins, expr->text, scope_binding_scope(scope, expr->text), type_is_named_generic(actual, "mutref"), reference_source_origin_is_local_storage(scope, expr->text));
    }
  }
  return false;
}

static bool expr_reference_provenance_as(const Program *program, const Expr *expr, Scope *scope, ValueProvenance *origins, bool mut_borrow) {
  ValueProvenance collected = {0};
  bool ok = expr_reference_provenance(program, expr, scope, &collected);
  if (ok) ok = value_provenance_add_all_as_with_prefix(origins, &collected, mut_borrow, NULL);
  value_provenance_free(&collected);
  return ok;
}

static bool check_borrow_conflict_at(Scope *scope, const char *root, const char *path, bool mut_borrow, ZDiag *diag, const Expr *expr) {
  size_t shared = 0;
  size_t mut = 0;
  scope_borrow_counts_for_place(scope, root, path, &shared, &mut);
  if (mut_borrow ? (shared > 0 || mut > 0) : (mut > 0)) {
    char place[200];
    format_origin_place(place, sizeof(place), root, path);
    char actual[160];
    snprintf(actual, sizeof(actual), "%.120s already has %s borrow", place, mut > 0 ? "a mutable" : "shared");
    set_diag_detail(diag, 3029, "borrow conflicts with an active lexical borrow", expr->line, expr->column, mut_borrow ? "unborrowed mutable lvalue" : "no active mutable borrow", actual, "end the earlier borrow's scope before borrowing again");
    ZBorrowTrace active[Z_BORROW_TRACE_MAX] = {0};
    size_t active_len = 0;
    bool truncated = false;
    if (scope_active_borrows_for_place(scope, root, path, !mut_borrow, active, &active_len, &truncated)) {
      set_diag_borrow_trace(diag, active, active_len, truncated, "move the conflicting borrow after the active borrow's lexical scope or put the earlier borrow in an inner block");
    }
    return false;
  }
  return true;
}

static bool check_place_root_available(const Expr *expr, Scope *scope, const char *root, ZDiag *diag) {
  if (!expr || !scope || !root || !root[0]) return true;
  const char *actual = scope_type(scope, root);
  if (actual && strcmp(actual, "Type") == 0) {
    char message[256];
    snprintf(message, sizeof(message), "type name '%s' cannot be used as a runtime value", root);
    return set_diag_detail(diag, 3005, message, expr->line, expr->column, "runtime value", "type name", "use the type name in an annotation or constructor context");
  }
  if (actual && type_is_owned(actual) && scope_is_moved(scope, root)) {
    char actual_detail[160];
    snprintf(actual_detail, sizeof(actual_detail), "%s was moved", root);
    return set_diag_detail(diag, 3013, "owned value was already moved", expr->line, expr->column, "live owned binding", actual_detail, "stop using the old binding after transferring ownership");
  }
  return true;
}

static bool check_read_not_mutably_borrowed(const Expr *expr, Scope *scope, ZDiag *diag) {
  char root[128];
  char path[256];
  if (!expr_binding_path(expr, root, sizeof(root), path, sizeof(path)) || !scope_has(scope, root)) return true;
  size_t shared = 0;
  size_t mut = 0;
  scope_borrow_counts_for_place(scope, root, path, &shared, &mut);
  (void)shared;
  if (mut == 0) return true;
  char place[200];
  format_origin_place(place, sizeof(place), root, path);
  char actual_detail[200];
  snprintf(actual_detail, sizeof(actual_detail), "%.160s has an active mutable borrow", place);
  set_diag_detail(diag, 3029, "cannot read a value while it is mutably borrowed", expr->line, expr->column, "unborrowed value or shared borrow only", actual_detail, "end the mutable borrow's lexical scope before reading the value");
  ZBorrowTrace active[Z_BORROW_TRACE_MAX] = {0};
  size_t active_len = 0;
  bool truncated = false;
  if (scope_active_borrows_for_place(scope, root, path, true, active, &active_len, &truncated)) {
    set_diag_borrow_trace(diag, active, active_len, truncated, "move the read after the mutable borrow's lexical scope or put the mutable borrow in an inner block");
  }
  return false;
}

static bool check_place_index_exprs(const Program *program, const Expr *expr, Scope *scope, ZDiag *diag) {
  if (!expr) return true;
  if (expr->kind == EXPR_IDENT) {
    if (!expr->resolved_type) set_expr_resolved_type(expr, expr_type(program, expr, scope));
    return true;
  }
  if (expr->kind == EXPR_MEMBER) {
    if (!check_place_index_exprs(program, expr->left, scope, diag)) return false;
    if (!expr->resolved_type) set_expr_resolved_type(expr, expr_type(program, expr, scope));
    return true;
  }
  if (expr->kind == EXPR_INDEX) {
    if (!check_place_index_exprs(program, expr->left, scope, diag)) return false;
    if (!check_expr_expected(program, expr->right, scope, diag, "usize")) return false;
    const char *index_type = expr_type(program, expr->right, scope);
    if (!is_int_type(index_type)) {
      return set_diag_detail(diag, 3028, "index expression must be an integer", expr->right ? expr->right->line : expr->line, expr->right ? expr->right->column : expr->column, "integer index", index_type, "use an integer expression such as usize or a checked integer literal");
    }
    if (!expr->resolved_type) set_expr_resolved_type(expr, expr_type(program, expr, scope));
  }
  return true;
}

static bool type_is_named_generic(const char *type, const char *name) {
  const char *inner = NULL;
  size_t inner_len = 0;
  return type_has_generic_arg(type, name, &inner, &inner_len);
}

static bool span_element_text(const char *type, char *out, size_t out_len) {
  if (!type || !out || out_len == 0) return false;
  const char *inner = NULL;
  size_t inner_len = 0;
  if (!type_has_generic_arg(type, "Span", &inner, &inner_len) &&
      !type_has_generic_arg(type, "MutSpan", &inner, &inner_len)) return false;
  snprintf(out, out_len, "%.*s", (int)inner_len, inner);
  return true;
}

static bool mutspan_element_text(const char *type, char *out, size_t out_len) {
  if (!type || !out || out_len == 0) return false;
  const char *inner = NULL;
  size_t inner_len = 0;
  if (!type_has_generic_arg(type, "MutSpan", &inner, &inner_len)) return false;
  snprintf(out, out_len, "%.*s", (int)inner_len, inner);
  return true;
}

static bool validate_type_form_inner(const char *type, ZDiag *diag, int line, int column, bool allow_self) {
  if (!type) return true;
  if (strcmp(type, "Self") == 0 && !allow_self) {
    return set_diag_detail(diag, 3046, "Self is only valid inside shape methods", line, column, "concrete type name outside shape methods", "Self", "use the declaring shape name or move this type into a shape method signature");
  }
  if (strncmp(type, "const ", strlen("const ")) == 0) return validate_type_form_inner(type + strlen("const "), diag, line, column, allow_self);
  if (type[0] == '[') {
    const char *close = strchr(type, ']');
    if (!close || close[1] == 0) return set_diag_detail(diag, 3015, "malformed fixed array type", line, column, "[N]T", type, "write a fixed length and element type");
    return validate_type_form_inner(close + 1, diag, line, column, allow_self);
  }
  if (strcmp(type, "Maybe") == 0 || strcmp(type, "Span") == 0 || strcmp(type, "MutSpan") == 0 || strcmp(type, "ref") == 0 || strcmp(type, "mutref") == 0 || strcmp(type, "owned") == 0) {
    return set_diag_detail(diag, 3015, "memory type requires one type argument", line, column, "Span<T>, MutSpan<T>, Maybe<T>, ref<T>, mutref<T>, or owned<T>", type, "add the missing type argument");
  }
  if (strncmp(type, "Maybe<", strlen("Maybe<")) == 0 || strncmp(type, "Span<", strlen("Span<")) == 0 || strncmp(type, "MutSpan<", strlen("MutSpan<")) == 0 ||
      strncmp(type, "ref<", strlen("ref<")) == 0 || strncmp(type, "mutref<", strlen("mutref<")) == 0 || strncmp(type, "owned<", strlen("owned<")) == 0) {
    const char *name = "ref";
    if (strncmp(type, "Maybe<", strlen("Maybe<")) == 0) name = "Maybe";
    else if (strncmp(type, "Span<", strlen("Span<")) == 0) name = "Span";
    else if (strncmp(type, "MutSpan<", strlen("MutSpan<")) == 0) name = "MutSpan";
    else if (strncmp(type, "mutref<", strlen("mutref<")) == 0) name = "mutref";
    else if (strncmp(type, "owned<", strlen("owned<")) == 0) name = "owned";
    const char *inner = NULL;
    size_t inner_len = 0;
    if (!type_has_generic_arg(type, name, &inner, &inner_len)) {
      return set_diag_detail(diag, 3015, "memory type expects exactly one type argument", line, column, "one nested type argument", type, "remove extra arguments or close the generic type");
    }
    char *inner_type = z_strndup(inner, inner_len);
    bool ok = validate_type_form_inner(inner_type, diag, line, column, allow_self);
    free(inner_type);
    return ok;
  }
  return true;
}

static bool validate_type_form(const char *type, ZDiag *diag, int line, int column) {
  return validate_type_form_inner(type, diag, line, column, false);
}

static bool validate_shape_method_type_form(const char *type, ZDiag *diag, int line, int column) {
  return validate_type_form_inner(type, diag, line, column, true);
}

static const TypeAlias *find_alias(const Program *program, const char *name) {
  if (!program || !name) return NULL;
  for (size_t i = 0; i < program->aliases.len; i++) {
    if (strcmp(program->aliases.items[i].name, name) == 0) return &program->aliases.items[i];
  }
  return NULL;
}

static const Program *current_type_program = NULL;

static const char *resolve_alias_type(const char *type) {
  if (!current_type_program || !type) return type;
  for (size_t depth = 0; depth < current_type_program->aliases.len; depth++) {
    const TypeAlias *alias = find_alias(current_type_program, type);
    if (!alias || !alias->target) return type;
    type = alias->target;
  }
  return type;
}

static bool check_expr(const Program *program, const Expr *expr, Scope *scope, ZDiag *diag);
static bool check_expr_expected(const Program *program, const Expr *expr, Scope *scope, ZDiag *diag, const char *expected);
static bool check_stmt_vec_with_loop(const Program *program, const Function *fun, const StmtVec *body, Scope *scope, ZDiag *diag, int loop_depth);
static bool stmt_vec_guarantees_exit(const StmtVec *body, bool function_raises);
static bool check_lvalue_target(const Program *program, const Expr *target, Scope *scope, ZDiag *diag, char *out_type, size_t out_type_len);
static const char *expr_type(const Program *program, const Expr *expr, Scope *scope);
static bool types_compatible(const char *expected, const char *actual);
static bool validate_type_names(const Program *program, const char *type, const ParamVec *primary, const ParamVec *secondary, bool allow_self, ZDiag *diag, int line, int column);
static const Shape *find_shape_for_type(const Program *program, const char *type);
static const Choice *find_choice(const Program *program, const char *name);
static const Param *find_case(const ParamVec *cases, const char *name);
static const Function *find_shape_method_decl(const Shape *shape, const char *name);
static void set_expr_resolved_type(const Expr *expr, const char *type);
static const Function *find_namespace_shape_method(const Program *program, const Expr *callee, const Shape **out_shape);
static const Function *find_constrained_interface_method_in_function(const Program *program, const Function *fun, const Expr *callee, const InterfaceDecl **out_interface);
static const Function *find_constrained_interface_method(const Program *program, const Expr *callee, const InterfaceDecl **out_interface);
static void strip_ref_like_type(const char *type, char *out, size_t out_len);
static bool interface_constraint_parts(const Program *program, const char *constraint, const InterfaceDecl **out_interface, char ***out_args, size_t *out_arg_len);
static void mark_owned_move_if_needed(const Expr *expr, Scope *scope, const char *destination_type);
static void mark_owned_target_live_if_needed(const Expr *target, Scope *scope, const char *target_type);
static int allow_fallible_call;
static const Function *checking_function;

static bool function_exists(const Program *program, const char *name) {
  if (strcmp(name, "expect") == 0) return true;
  for (size_t i = 0; i < program->functions.len; i++) {
    if (strcmp(program->functions.items[i].name, name) == 0) return true;
  }
  return false;
}

static const Function *find_function(const Program *program, const char *name) {
  for (size_t i = 0; i < program->functions.len; i++) {
    if (strcmp(program->functions.items[i].name, name) == 0) return &program->functions.items[i];
  }
  return NULL;
}

typedef struct {
  const char *name;
  char *type;
} GenericBinding;

typedef enum {
  PROVENANCE_CALL_FUNCTION,
  PROVENANCE_CALL_SHAPE_NAMESPACE,
  PROVENANCE_CALL_RECEIVER,
  PROVENANCE_CALL_CONSTRAINED_INTERFACE,
  PROVENANCE_CALL_CONCRETE_CONSTRAINED_SHAPE
} ProvenanceCallKind;

typedef struct {
  ProvenanceCallKind kind;
  const Function *callee;
  const Expr *call;
  const Expr *receiver;
  size_t param_offset;
  const Shape *shape;
  const InterfaceDecl *interface;
  GenericBinding *bindings;
  size_t binding_len;
  char *return_type;
} ResolvedProvenanceCall;

static bool apply_expr_call_storage_effects(
  const Program *program,
  const Expr *expr,
  Scope *scope,
  ZDiag *diag
);

static bool apply_checked_call_storage_effects(
  const Program *program,
  const Expr *expr,
  Scope *scope,
  ZDiag *diag
);

static bool function_is_generic(const Function *fun) {
  return fun && fun->type_params.len > 0;
}

static const TypeArgVec *call_type_args(const Expr *call) {
  if (!call) return NULL;
  if (call->type_args.len > 0) return &call->type_args;
  if (call->kind == EXPR_CALL && call->left && call->left->type_args.len > 0) return &call->left->type_args;
  return &call->type_args;
}

static const char *generic_binding_lookup(GenericBinding *bindings, size_t len, const char *name) {
  for (size_t i = 0; i < len; i++) {
    if (strcmp(bindings[i].name, name) == 0) return bindings[i].type;
  }
  return NULL;
}

static void generic_bindings_free(GenericBinding *bindings, size_t len) {
  for (size_t i = 0; i < len; i++) free(bindings[i].type);
}

static void resolved_provenance_call_free(ResolvedProvenanceCall *resolved) {
  if (!resolved) return;
  generic_bindings_free(resolved->bindings, resolved->binding_len);
  free(resolved->bindings);
  free(resolved->return_type);
  *resolved = (ResolvedProvenanceCall){0};
}

static bool generic_binding_set(GenericBinding *bindings, size_t len, const char *name, const char *type) {
  for (size_t i = 0; i < len; i++) {
    if (strcmp(bindings[i].name, name) == 0) {
      if (!bindings[i].type) {
        bindings[i].type = z_strdup(type ? type : "Unknown");
        return true;
      }
      return type && types_compatible(bindings[i].type, type);
    }
  }
  return true;
}

static bool type_is_generic_param(const Function *fun, const char *type) {
  if (!function_is_generic(fun) || !type) return false;
  for (size_t i = 0; i < fun->type_params.len; i++) {
    if (strcmp(fun->type_params.items[i].name, type) == 0) return true;
  }
  return false;
}

static bool is_static_int_param_type(const char *type) {
  return type && (strcmp(type, "usize") == 0 || strcmp(type, "isize") == 0 ||
                  strcmp(type, "u8") == 0 || strcmp(type, "u16") == 0 || strcmp(type, "u32") == 0 || strcmp(type, "u64") == 0 ||
                  strcmp(type, "i8") == 0 || strcmp(type, "i16") == 0 || strcmp(type, "i32") == 0 || strcmp(type, "i64") == 0);
}

static bool enum_decl_has_case(const EnumDecl *item_enum, const char *case_name) {
  if (!item_enum || !case_name) return false;
  for (size_t i = 0; i < item_enum->cases.len; i++) {
    if (strcmp(item_enum->cases.items[i].name, case_name) == 0) return true;
  }
  return false;
}

static const EnumDecl *static_enum_type(const Program *program, const char *type) {
  if (!program || !type) return NULL;
  for (size_t i = 0; i < program->enums.len; i++) {
    if (strcmp(program->enums.items[i].name, type) == 0) return &program->enums.items[i];
  }
  return NULL;
}

static bool is_static_value_param_type(const Program *program, const char *type) {
  return is_static_int_param_type(type) || (type && strcmp(type, "Bool") == 0) || static_enum_type(program, type) != NULL;
}

static const char *static_value_expected_label(const Program *program, const char *type) {
  if (is_static_int_param_type(type)) return "integer, integer const, or integer meta result";
  if (type && strcmp(type, "Bool") == 0) return "Bool literal, Bool const, or Bool meta result";
  if (static_enum_type(program, type)) return "enum case such as Mode.on or a const enum value";
  return "integer, Bool, or enum static value";
}

static bool parse_static_uint_text(const char *text, unsigned long long *out) {
  if (!text || !text[0]) return false;
  size_t text_len = strlen(text);
  size_t body_len = text_len;
  const char *last_underscore = strrchr(text, '_');
  if (last_underscore && isalpha((unsigned char)last_underscore[1])) body_len = (size_t)(last_underscore - text);
  if (body_len == 0) return false;
  unsigned radix = 10;
  size_t index = 0;
  if (body_len > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    radix = 16;
    index = 2;
  } else if (body_len > 2 && text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) {
    radix = 2;
    index = 2;
  } else if (body_len > 2 && text[0] == '0' && (text[1] == 'o' || text[1] == 'O')) {
    radix = 8;
    index = 2;
  }
  unsigned long long value = 0;
  bool saw_digit = false;
  for (; index < body_len; index++) {
    char ch = text[index];
    if (ch == '_') continue;
    int digit = -1;
    if (ch >= '0' && ch <= '9') digit = ch - '0';
    else if (ch >= 'a' && ch <= 'f') digit = 10 + (ch - 'a');
    else if (ch >= 'A' && ch <= 'F') digit = 10 + (ch - 'A');
    if (digit < 0 || (unsigned)digit >= radix) return false;
    if (value > (ULLONG_MAX - (unsigned)digit) / radix) return false;
    value = value * radix + (unsigned)digit;
    saw_digit = true;
  }
  if (!saw_digit) return false;
  if (out) *out = value;
  return true;
}

static bool eval_meta_value(const Program *program, const Expr *expr, MetaValue *out, size_t depth, size_t *steps);
static bool static_value_from_expr(const Program *program, const Expr *expr, StaticValue *out, size_t depth);

static bool static_enum_value_from_text(const Program *program, const char *text, StaticValue *out) {
  if (!program || !text) return false;
  const char *dot = strchr(text, '.');
  if (!dot || dot == text || dot[1] == 0 || strchr(dot + 1, '.')) return false;
  char enum_type[64];
  char enum_case[64];
  snprintf(enum_type, sizeof(enum_type), "%.*s", (int)(dot - text), text);
  snprintf(enum_case, sizeof(enum_case), "%s", dot + 1);
  const EnumDecl *item_enum = static_enum_type(program, enum_type);
  if (!enum_decl_has_case(item_enum, enum_case)) return false;
  if (out) {
    *out = (StaticValue){.kind = STATIC_VALUE_ENUM};
    snprintf(out->enum_type, sizeof(out->enum_type), "%s", enum_type);
    snprintf(out->enum_case, sizeof(out->enum_case), "%s", enum_case);
  }
  return true;
}

static bool static_value_from_text(const Program *program, const char *text, StaticValue *out) {
  unsigned long long value = 0;
  if (parse_static_uint_text(text, &value)) {
    if (out) *out = (StaticValue){.kind = STATIC_VALUE_INTEGER, .number = value};
    return true;
  }
  if (text && (strcmp(text, "true") == 0 || strcmp(text, "false") == 0)) {
    if (out) *out = (StaticValue){.kind = STATIC_VALUE_BOOL, .boolean = strcmp(text, "true") == 0};
    return true;
  }
  if (static_enum_value_from_text(program, text, out)) return true;
  for (size_t i = 0; program && text && i < program->consts.len; i++) {
    if (strcmp(program->consts.items[i].name, text) == 0) {
      return static_value_from_expr(program, program->consts.items[i].expr, out, 0);
    }
  }
  return false;
}

static bool static_value_from_meta(const MetaValue *meta, StaticValue *out) {
  if (!meta) return false;
  if (meta->kind == META_VALUE_NUMBER) {
    if (out) *out = (StaticValue){.kind = STATIC_VALUE_INTEGER, .number = meta->number};
    return true;
  }
  if (meta->kind == META_VALUE_BOOL) {
    if (out) *out = (StaticValue){.kind = STATIC_VALUE_BOOL, .boolean = meta->boolean};
    return true;
  }
  return false;
}

static bool static_value_from_expr(const Program *program, const Expr *expr, StaticValue *out, size_t depth) {
  if (!expr || depth > 64) return false;
  if (expr->kind == EXPR_MEMBER && expr->left && expr->left->kind == EXPR_IDENT) {
    char text[128];
    snprintf(text, sizeof(text), "%s.%s", expr->left->text ? expr->left->text : "", expr->text ? expr->text : "");
    if (static_enum_value_from_text(program, text, out)) return true;
  }
  MetaValue meta = {0};
  size_t steps = 0;
  if (eval_meta_value(program, expr, &meta, depth, &steps) && static_value_from_meta(&meta, out)) return true;
  return false;
}

static bool static_value_matches_type(const Program *program, const StaticValue *value, const char *type) {
  if (!value) return false;
  if (is_static_int_param_type(type)) return value->kind == STATIC_VALUE_INTEGER;
  if (type && strcmp(type, "Bool") == 0) return value->kind == STATIC_VALUE_BOOL;
  const EnumDecl *item_enum = static_enum_type(program, type);
  return item_enum && value->kind == STATIC_VALUE_ENUM && strcmp(value->enum_type, type) == 0 && enum_decl_has_case(item_enum, value->enum_case);
}

static char *static_value_canonical_text(const StaticValue *value) {
  if (!value) return NULL;
  ZBuf buf;
  zbuf_init(&buf);
  if (value->kind == STATIC_VALUE_INTEGER) zbuf_appendf(&buf, "%llu", value->number);
  else if (value->kind == STATIC_VALUE_BOOL) zbuf_append(&buf, value->boolean ? "true" : "false");
  else if (value->kind == STATIC_VALUE_ENUM) zbuf_appendf(&buf, "%s.%s", value->enum_type, value->enum_case);
  else {
    zbuf_free(&buf);
    return NULL;
  }
  return buf.data;
}

static char *canonical_static_arg(const Program *program, const char *text) {
  StaticValue value = {0};
  if (!static_value_from_text(program, text, &value)) return NULL;
  return static_value_canonical_text(&value);
}

static char *canonical_static_arg_for_type(const Program *program, const char *text, const char *type) {
  StaticValue value = {0};
  if (!static_value_from_text(program, text, &value) || !static_value_matches_type(program, &value, type)) return NULL;
  return static_value_canonical_text(&value);
}

static bool normalize_static_bindings(const Program *program, const Function *fun, const Expr *call, ZDiag *diag, GenericBinding *bindings, size_t binding_len) {
  if (!function_is_generic(fun)) return true;
  for (size_t i = 0; i < fun->type_params.len && i < binding_len; i++) {
    const Param *param = &fun->type_params.items[i];
    if (!param->is_static) continue;
    if (!is_static_value_param_type(program, param->type)) {
      return set_diag_detail(diag, 3043, "static value parameter type is not supported", param->line, param->column, "integer, Bool, or enum static parameter", param->type ? param->type : "Unknown", "use a concrete integer, Bool, or enum type for this static parameter");
    }
    char *canonical = canonical_static_arg_for_type(program, bindings[i].type, param->type);
    if (!canonical) {
      return set_diag_detail(diag, 3044, "static value argument must be deterministic and concrete", call ? call->line : param->line, call ? call->column : param->column, static_value_expected_label(program, param->type), bindings[i].type ? bindings[i].type : "Unknown", "pass an explicit literal, top-level const, or supported meta value with the static parameter type");
    }
    free(bindings[i].type);
    bindings[i].type = canonical;
  }
  return true;
}

static char *type_substitute_generic(const char *type, GenericBinding *bindings, size_t binding_len) {
  if (!type) return z_strdup("Unknown");
  const char *bound = generic_binding_lookup(bindings, binding_len, type);
  if (bound) return z_strdup(bound);
  if (strncmp(type, "const ", strlen("const ")) == 0) {
    char *inner = type_substitute_generic(type + strlen("const "), bindings, binding_len);
    ZBuf buf;
    zbuf_init(&buf);
    zbuf_append(&buf, "const ");
    zbuf_append(&buf, inner);
    free(inner);
    return buf.data;
  }
  if (type[0] == '[') {
    const char *close = strchr(type, ']');
    if (close && close[1]) {
      char *length = z_strndup(type + 1, (size_t)(close - type - 1));
      const char *length_bound = generic_binding_lookup(bindings, binding_len, length);
      char *inner = type_substitute_generic(close + 1, bindings, binding_len);
      ZBuf buf;
      zbuf_init(&buf);
      zbuf_append_char(&buf, '[');
      zbuf_append(&buf, length_bound ? length_bound : length);
      zbuf_append_char(&buf, ']');
      zbuf_append(&buf, inner);
      free(length);
      free(inner);
      return buf.data;
    }
  }
  const char *generic_names[] = {"Maybe", "Span", "MutSpan", "ref", "mutref", "owned", NULL};
  for (size_t i = 0; generic_names[i]; i++) {
    const char *inner = NULL;
    size_t inner_len = 0;
    if (type_has_generic_arg(type, generic_names[i], &inner, &inner_len)) {
      char *inner_text = z_strndup(inner, inner_len);
      char *substituted = type_substitute_generic(inner_text, bindings, binding_len);
      ZBuf buf;
      zbuf_init(&buf);
      zbuf_appendf(&buf, "%s<%s>", generic_names[i], substituted);
      free(inner_text);
      free(substituted);
      return buf.data;
    }
  }
  const char *open = strchr(type, '<');
  const char *close = strrchr(type, '>');
  if (open && close && close[1] == 0 && open > type) {
    char *name = z_strndup(type, (size_t)(open - type));
    char **args = NULL;
    size_t arg_len = 0;
    if (type_generic_arg_list(type, name, &args, &arg_len)) {
      ZBuf buf;
      zbuf_init(&buf);
      zbuf_append(&buf, name);
      zbuf_append_char(&buf, '<');
      for (size_t arg_index = 0; arg_index < arg_len; arg_index++) {
        if (arg_index > 0) zbuf_append(&buf, ",");
        char *substituted = type_substitute_generic(args[arg_index], bindings, binding_len);
        zbuf_append(&buf, substituted);
        free(substituted);
      }
      zbuf_append_char(&buf, '>');
      free_type_arg_list(args, arg_len);
      free(name);
      return buf.data;
    }
    free(name);
  }
  return z_strdup(type);
}

static GenericBinding *return_provenance_expr_bindings = NULL;
static size_t return_provenance_expr_binding_len = 0;

static const char *expr_resolved_type_for_current_context(const Expr *expr) {
  if (!expr || !expr->resolved_type) return NULL;
  if (return_provenance_expr_bindings && return_provenance_expr_binding_len > 0) {
    static char substituted_type[256];
    char *substituted = type_substitute_generic(expr->resolved_type, return_provenance_expr_bindings, return_provenance_expr_binding_len);
    snprintf(substituted_type, sizeof(substituted_type), "%s", substituted ? substituted : "Unknown");
    free(substituted);
    return substituted_type;
  }
  return expr->resolved_type;
}

static char *provenance_context_type_text(const char *type, GenericBinding *bindings, size_t binding_len) {
  if (bindings && binding_len > 0) return type_substitute_generic(type, bindings, binding_len);
  if (return_provenance_expr_bindings && return_provenance_expr_binding_len > 0) {
    return type_substitute_generic(type, return_provenance_expr_bindings, return_provenance_expr_binding_len);
  }
  return z_strdup(type ? type : "Unknown");
}

static void provenance_context_substitute_bindings(GenericBinding *bindings, size_t binding_len, GenericBinding *context_bindings, size_t context_binding_len) {
  if (!bindings || binding_len == 0) return;
  GenericBinding *source_bindings = context_bindings;
  size_t source_len = context_binding_len;
  if ((!source_bindings || source_len == 0) && return_provenance_expr_bindings && return_provenance_expr_binding_len > 0) {
    source_bindings = return_provenance_expr_bindings;
    source_len = return_provenance_expr_binding_len;
  }
  if (!source_bindings || source_len == 0) return;
  for (size_t i = 0; i < binding_len; i++) {
    if (!bindings[i].type) continue;
    char *substituted = type_substitute_generic(bindings[i].type, source_bindings, source_len);
    free(bindings[i].type);
    bindings[i].type = substituted;
  }
}

static bool infer_generic_type_from_pattern(const Function *fun, const char *pattern, const char *actual, GenericBinding *bindings, size_t binding_len) {
  if (!pattern || !actual) return true;
  if (type_is_generic_param(fun, pattern)) return generic_binding_set(bindings, binding_len, pattern, actual);
  const char *generic_names[] = {"Maybe", "Span", "MutSpan", "ref", "mutref", "owned", NULL};
  for (size_t i = 0; generic_names[i]; i++) {
    const char *pattern_inner = NULL;
    const char *actual_inner = NULL;
    size_t pattern_len = 0;
    size_t actual_len = 0;
    if (type_has_generic_arg(pattern, generic_names[i], &pattern_inner, &pattern_len) &&
        type_has_generic_arg(actual, generic_names[i], &actual_inner, &actual_len)) {
      char *pattern_text = z_strndup(pattern_inner, pattern_len);
      char *actual_text = z_strndup(actual_inner, actual_len);
      bool ok = infer_generic_type_from_pattern(fun, pattern_text, actual_text, bindings, binding_len);
      free(pattern_text);
      free(actual_text);
      return ok;
    }
  }
  const char *pattern_open = strchr(pattern, '<');
  const char *actual_open = strchr(actual, '<');
  if (pattern_open && actual_open && pattern_open > pattern && actual_open > actual) {
    size_t name_len = (size_t)(pattern_open - pattern);
    if (strncmp(pattern, actual, name_len) == 0 && actual[name_len] == '<') {
      char *name = z_strndup(pattern, name_len);
      char **pattern_args = NULL;
      char **actual_args = NULL;
      size_t pattern_arg_len = 0;
      size_t actual_arg_len = 0;
      bool ok = type_generic_arg_list(pattern, name, &pattern_args, &pattern_arg_len) &&
                type_generic_arg_list(actual, name, &actual_args, &actual_arg_len) &&
                pattern_arg_len == actual_arg_len;
      if (ok) {
        for (size_t arg_index = 0; arg_index < pattern_arg_len && ok; arg_index++) {
          ok = infer_generic_type_from_pattern(fun, pattern_args[arg_index], actual_args[arg_index], bindings, binding_len);
        }
      }
      free_type_arg_list(pattern_args, pattern_arg_len);
      free_type_arg_list(actual_args, actual_arg_len);
      free(name);
      return ok;
    }
  }
  return true;
}

static bool build_generic_bindings(const Program *program, const Function *fun, const Expr *call, Scope *scope, ZDiag *diag, GenericBinding *bindings, size_t binding_len, const char *expected_return) {
  (void)program;
  if (!function_is_generic(fun)) return true;
  const TypeArgVec *type_args = call_type_args(call);
  if (type_args && type_args->len > 0) {
    if (type_args->len != fun->type_params.len) {
      return set_diag_detail(diag, 3032, "generic call type argument count mismatch", call->line, call->column, "one type argument per generic parameter", "wrong generic argument count", "pass explicit type arguments matching the function declaration");
    }
    for (size_t i = 0; i < fun->type_params.len; i++) bindings[i].type = z_strdup(type_args->items[i].type);
    return normalize_static_bindings(program, fun, call, diag, bindings, binding_len);
  }
  for (size_t i = 0; i < fun->params.len && i < call->args.len; i++) {
    const char *actual = expr_type(program, call->args.items[i], scope);
    if (!infer_generic_type_from_pattern(fun, fun->params.items[i].type, actual, bindings, binding_len)) {
      return set_diag_detail(diag, 3033, "generic inference found conflicting argument types", call->args.items[i]->line, call->args.items[i]->column, "one concrete type for each generic parameter", actual, "pass explicit type arguments or make argument types match");
    }
  }
  if (expected_return && fun->return_type && !infer_generic_type_from_pattern(fun, fun->return_type, expected_return, bindings, binding_len)) {
    return set_diag_detail(diag, 3033, "generic inference found conflicting expected return type", call->line, call->column, "one concrete type for each generic parameter", expected_return, "pass explicit type arguments or adjust the expected return type");
  }
  for (size_t i = 0; i < binding_len; i++) {
    if (!bindings[i].type) {
      return set_diag_detail(diag, 3034, "generic type argument cannot be inferred", call->line, call->column, "explicit generic type argument", fun->type_params.items[i].name, "call the function with explicit type arguments such as name<i32>(value)");
    }
  }
  return normalize_static_bindings(program, fun, call, diag, bindings, binding_len);
}

static bool validate_generic_constraints(const Program *program, const Function *fun, const Expr *call, ZDiag *diag, GenericBinding *bindings, size_t binding_len) {
  if (!function_is_generic(fun)) return true;
  for (size_t param_index = 0; param_index < fun->type_params.len; param_index++) {
    const Param *type_param = &fun->type_params.items[param_index];
    if (type_param->is_static) continue;
    if (!type_param->type || strcmp(type_param->type, "Type") == 0) continue;
    const InterfaceDecl *interface = NULL;
    char **constraint_args = NULL;
    size_t constraint_arg_len = 0;
    if (!interface_constraint_parts(program, type_param->type, &interface, &constraint_args, &constraint_arg_len) || !interface) {
      char message[256];
      snprintf(message, sizeof(message), "generic parameter '%s' references an unknown interface constraint", type_param->name);
      free_type_arg_list(constraint_args, constraint_arg_len);
      return set_diag_detail(diag, 3038, message, type_param->line, type_param->column, "declared interface constraint", type_param->type, "declare the interface or change the constraint to Type");
    }
    const char *actual_type = generic_binding_lookup(bindings, binding_len, type_param->name);
    const Shape *shape = find_shape_for_type(program, actual_type);
    if (!shape) {
      char message[256];
      snprintf(message, sizeof(message), "type argument for '%s' does not satisfy interface '%s'", type_param->name, interface->name);
      free_type_arg_list(constraint_args, constraint_arg_len);
      return set_diag_detail(diag, 3038, message, call->line, call->column, "concrete shape type", actual_type ? actual_type : "Unknown", "pass a shape with matching static methods");
    }
    GenericBinding *interface_bindings = z_checked_calloc(interface->type_params.len, sizeof(GenericBinding));
    for (size_t i = 0; i < interface->type_params.len; i++) {
      interface_bindings[i].name = interface->type_params.items[i].name;
      interface_bindings[i].type = type_substitute_generic(i < constraint_arg_len ? constraint_args[i] : "Unknown", bindings, binding_len);
    }
    GenericBinding self_binding = {.name = "Self", .type = (char *)(actual_type ? actual_type : shape->name)};
    for (size_t method_index = 0; method_index < interface->methods.len; method_index++) {
      const Function *required = &interface->methods.items[method_index];
      const Function *method = find_shape_method_decl(shape, required->name);
      if (!method) {
        char message[256];
        snprintf(message, sizeof(message), "shape '%s' is missing interface method '%s.%s'", actual_type ? actual_type : shape->name, interface->name, required->name);
        free_type_arg_list(constraint_args, constraint_arg_len);
        generic_bindings_free(interface_bindings, interface->type_params.len);
        free(interface_bindings);
        return set_diag_detail(diag, 3039, message, call->line, call->column, "matching static method on the concrete shape", "method not found", "add the required static method with the interface signature");
      }
      if (method->params.len != required->params.len) {
        char message[256];
        snprintf(message, sizeof(message), "interface method '%s.%s' parameter count does not match shape method", interface->name, required->name);
        free_type_arg_list(constraint_args, constraint_arg_len);
        generic_bindings_free(interface_bindings, interface->type_params.len);
        free(interface_bindings);
        return set_diag_detail(diag, 3040, message, method->line, method->column, "same parameter count as interface", "different parameter count", "update the shape method signature to match the interface");
      }
      for (size_t i = 0; i < required->params.len; i++) {
        char *expected = type_substitute_generic(required->params.items[i].type, interface_bindings, interface->type_params.len);
        char *actual = type_substitute_generic(method->params.items[i].type, &self_binding, 1);
        bool ok = types_compatible(expected, actual);
        if (!ok) {
          char message[256];
          snprintf(message, sizeof(message), "interface method '%s.%s' parameter %zu does not match shape method", interface->name, required->name, i + 1);
          bool diag_ok = set_diag_detail(diag, 3042, message, method->params.items[i].line, method->params.items[i].column, expected, actual, "use the parameter type required by the interface");
          free(expected);
          free(actual);
          free_type_arg_list(constraint_args, constraint_arg_len);
          generic_bindings_free(interface_bindings, interface->type_params.len);
          free(interface_bindings);
          return diag_ok;
        }
        free(expected);
        free(actual);
      }
      char *expected_return = type_substitute_generic(required->return_type, interface_bindings, interface->type_params.len);
      char *actual_return = type_substitute_generic(method->return_type, &self_binding, 1);
      if (!types_compatible(expected_return, actual_return)) {
        char message[256];
        snprintf(message, sizeof(message), "interface method '%s.%s' return type does not match shape method", interface->name, required->name);
        bool ok = set_diag_detail(diag, 3041, message, method->line, method->column, expected_return, actual_return, "return the type required by the interface");
        free(expected_return);
        free(actual_return);
        free_type_arg_list(constraint_args, constraint_arg_len);
        generic_bindings_free(interface_bindings, interface->type_params.len);
        free(interface_bindings);
        return ok;
      }
      free(expected_return);
      free(actual_return);
    }
    free_type_arg_list(constraint_args, constraint_arg_len);
    generic_bindings_free(interface_bindings, interface->type_params.len);
    free(interface_bindings);
  }
  return true;
}

static bool function_error_contains(const Function *fun, const char *name) {
  if (!fun || !name) return false;
  for (size_t i = 0; i < fun->errors.len; i++) {
    if (fun->errors.items[i].name && strcmp(fun->errors.items[i].name, name) == 0) return true;
  }
  return false;
}

static const Function *fallible_callee(const Program *program, const Expr *expr);
static const Function *fallible_callee_in_context(const Program *program, const Function *context_fun, Scope *scope, const Expr *expr);
static bool is_builtin_fallible_call(const Expr *expr);
static bool function_error_sets_include_builtin(const Function *caller, ZDiag *diag, const Expr *call);
static bool function_error_sets_compatible_inner(const Function *caller, const Function *callee, ZDiag *diag, const Expr *call, size_t depth);
static bool stmt_vec_raise_errors_covered(const Program *program, const StmtVec *body, const Function *caller, const Function *context_fun, Scope *scope, const Expr *call, ZDiag *diag, size_t depth);

static bool expr_raise_errors_covered(const Program *program, const Expr *expr, const Function *caller, const Function *context_fun, Scope *scope, const Expr *call, ZDiag *diag, size_t depth) {
  if (!expr || depth > 64) return true;
  if (expr->kind == EXPR_CHECK) {
    if (is_builtin_fallible_call(expr->left) && !function_error_sets_include_builtin(caller, diag, call)) return false;
    const Function *callee = fallible_callee_in_context(program, context_fun, scope, expr->left);
    if (callee && !function_error_sets_compatible_inner(caller, callee, diag, call, depth + 1)) return false;
    return expr_raise_errors_covered(program, expr->left, caller, context_fun, scope, call, diag, depth);
  }
  if (expr->kind == EXPR_RESCUE) return expr_raise_errors_covered(program, expr->right, caller, context_fun, scope, call, diag, depth);
  if (!expr_raise_errors_covered(program, expr->left, caller, context_fun, scope, call, diag, depth) ||
      !expr_raise_errors_covered(program, expr->right, caller, context_fun, scope, call, diag, depth)) return false;
  for (size_t i = 0; i < expr->args.len; i++) {
    if (!expr_raise_errors_covered(program, expr->args.items[i], caller, context_fun, scope, call, diag, depth)) return false;
  }
  for (size_t i = 0; i < expr->fields.len; i++) {
    if (!expr_raise_errors_covered(program, expr->fields.items[i].value, caller, context_fun, scope, call, diag, depth)) return false;
  }
  return true;
}

static void flow_scope_add_stmt_binding(const Program *program, const Stmt *stmt, Scope *scope) {
  if (!stmt || stmt->kind != STMT_LET || !stmt->name || !scope) return;
  const char *binding_type = stmt->resolved_type ? stmt->resolved_type : stmt->type;
  if (!binding_type && stmt->expr) binding_type = expr_type(program, stmt->expr, scope);
  scope_add(scope, stmt->name, binding_type ? binding_type : "Unknown", stmt->mutable_binding);
}

static void flow_scope_add_function_bindings(const Function *fun, Scope *scope) {
  if (!fun || !scope) return;
  for (size_t type_param_index = 0; type_param_index < fun->type_params.len; type_param_index++) {
    const Param *type_param = &fun->type_params.items[type_param_index];
    scope_add(scope, type_param->name, type_param->is_static ? (type_param->type ? type_param->type : "usize") : "Type", false);
  }
  for (size_t param_index = 0; param_index < fun->params.len; param_index++) {
    const Param *param = &fun->params.items[param_index];
    scope_add_param_decl(scope, param->name, param->type, param->line, param->column);
  }
}

static bool stmt_raise_errors_covered(const Program *program, const Stmt *stmt, const Function *caller, const Function *context_fun, Scope *scope, const Expr *call, ZDiag *diag, size_t depth) {
  if (!stmt) return true;
  if (depth > 64) return true;
  if (stmt->kind == STMT_RAISE && stmt->name && !function_error_contains(caller, stmt->name)) {
    char actual[160];
    snprintf(actual, sizeof(actual), "callee may raise %s", stmt->name);
    return set_diag_detail(diag, 1002, "caller error set does not include inferred callee error", call->line, call->column, "caller raises set containing every checked callee error", actual, "add the missing error to the caller's raises { ... } set");
  }
  if (stmt->kind == STMT_CHECK) {
    if (is_builtin_fallible_call(stmt->expr) && !function_error_sets_include_builtin(caller, diag, call)) return false;
    const Function *callee = fallible_callee_in_context(program, context_fun, scope, stmt->expr);
    if (callee && !function_error_sets_compatible_inner(caller, callee, diag, call, depth + 1)) return false;
  }
  if (!expr_raise_errors_covered(program, stmt->target, caller, context_fun, scope, call, diag, depth) ||
      !expr_raise_errors_covered(program, stmt->expr, caller, context_fun, scope, call, diag, depth) ||
      !expr_raise_errors_covered(program, stmt->range_end, caller, context_fun, scope, call, diag, depth)) return false;
  if (stmt->kind == STMT_IF) {
    Scope then_scope = {.parent = scope};
    Scope else_scope = {.parent = scope};
    bool ok = stmt_vec_raise_errors_covered(program, &stmt->then_body, caller, context_fun, &then_scope, call, diag, depth) &&
              stmt_vec_raise_errors_covered(program, &stmt->else_body, caller, context_fun, &else_scope, call, diag, depth);
    scope_free(&then_scope);
    scope_free(&else_scope);
    if (!ok) return false;
    return true;
  }
  if (stmt->kind == STMT_WHILE) {
    Scope then_scope = {.parent = scope};
    bool ok = stmt_vec_raise_errors_covered(program, &stmt->then_body, caller, context_fun, &then_scope, call, diag, depth);
    scope_free(&then_scope);
    if (!ok) return false;
  } else if (stmt->kind == STMT_FOR) {
    Scope body_scope = {.parent = scope};
    const char *iter_type = stmt->resolved_type ? stmt->resolved_type : (stmt->expr ? expr_type(program, stmt->expr, scope) : "Unknown");
    if (stmt->name) scope_add(&body_scope, stmt->name, iter_type ? iter_type : "Unknown", false);
    bool ok = stmt_vec_raise_errors_covered(program, &stmt->then_body, caller, context_fun, &body_scope, call, diag, depth);
    scope_free(&body_scope);
    if (!ok) return false;
  } else if (!stmt_vec_raise_errors_covered(program, &stmt->then_body, caller, context_fun, scope, call, diag, depth)) {
    return false;
  }
  if (!stmt_vec_raise_errors_covered(program, &stmt->else_body, caller, context_fun, scope, call, diag, depth)) return false;
  for (size_t i = 0; i < stmt->match_arms.len; i++) {
    const MatchArm *arm = &stmt->match_arms.items[i];
    if (!expr_raise_errors_covered(program, arm->guard, caller, context_fun, scope, call, diag, depth)) return false;
    Scope arm_scope = {.parent = scope};
    if (stmt->kind == STMT_MATCH && arm->payload_name && stmt->expr) {
      const char *match_type = stmt->resolved_type ? stmt->resolved_type : expr_type(program, stmt->expr, scope);
      const Choice *item_choice = find_choice(program, match_type);
      const Param *item_case = item_choice ? find_case(&item_choice->cases, arm->case_name) : NULL;
      if (item_case && item_case->type) scope_add(&arm_scope, arm->payload_name, item_case->type, false);
    }
    bool ok = stmt_vec_raise_errors_covered(program, &arm->body, caller, context_fun, &arm_scope, call, diag, depth);
    scope_free(&arm_scope);
    if (!ok) return false;
  }
  return true;
}

static bool stmt_vec_raise_errors_covered(const Program *program, const StmtVec *body, const Function *caller, const Function *context_fun, Scope *scope, const Expr *call, ZDiag *diag, size_t depth) {
  if (!body) return true;
  for (size_t i = 0; i < body->len; i++) {
    const Stmt *stmt = body->items[i];
    if (!stmt_raise_errors_covered(program, stmt, caller, context_fun, scope, call, diag, depth)) return false;
    flow_scope_add_stmt_binding(program, stmt, scope);
  }
  return true;
}

static bool function_error_sets_compatible_inner(const Function *caller, const Function *callee, ZDiag *diag, const Expr *call, size_t depth) {
  if (!caller || !callee || !callee->raises) return true;
  if (depth > 64) return true;
  if (!caller->raises) {
    return set_diag_detail(diag, 1001, "fallible call requires function to be marked raises", call->line, call->column, "function signature marked raises", "function is not marked raises", "add raises to the function signature or handle the error locally");
  }
  if (!caller->has_error_set) return true;
  if (!callee->has_error_set) {
    Scope flow_scope = {0};
    flow_scope_add_function_bindings(callee, &flow_scope);
    bool ok = stmt_vec_raise_errors_covered(current_type_program, &callee->body, caller, callee, &flow_scope, call, diag, depth + 1);
    scope_free(&flow_scope);
    return ok;
  }
  for (size_t i = 0; i < callee->errors.len; i++) {
    const char *error_name = callee->errors.items[i].name;
    if (!function_error_contains(caller, error_name)) {
      char actual[160];
      snprintf(actual, sizeof(actual), "callee may raise %s", error_name ? error_name : "<error>");
      return set_diag_detail(diag, 1002, "caller error set does not include callee error", call->line, call->column, "caller raises set containing every checked callee error", actual, "add the missing error to the caller's raises { ... } set");
    }
  }
  return true;
}

static bool function_error_sets_compatible(const Function *caller, const Function *callee, ZDiag *diag, const Expr *call) {
  return function_error_sets_compatible_inner(caller, callee, diag, call, 0);
}

static bool function_has_error_flow_inner(const Program *program, const Function *fun, size_t depth);

static bool expr_is_world_stream_write_shape(const Expr *expr) {
  if (!expr || expr->kind != EXPR_CALL || !expr->left || expr->left->kind != EXPR_MEMBER) return false;
  const Expr *write = expr->left;
  if (!write->text || strcmp(write->text, "write") != 0) return false;
  const Expr *stream = write->left;
  return stream && stream->kind == EXPR_MEMBER && stream->text &&
         (strcmp(stream->text, "out") == 0 || strcmp(stream->text, "err") == 0);
}

static const Function *resolve_call_function_in_context(const Program *program, const Function *context_fun, Scope *scope, const Expr *expr) {
  if (!expr || expr->kind != EXPR_CALL || !expr->left) return NULL;
  const Function *fun = NULL;
  if (expr->left->kind == EXPR_IDENT) {
    fun = find_function(program, expr->left->text);
  } else if (expr->left->kind == EXPR_MEMBER) {
    fun = find_namespace_shape_method(program, expr->left, NULL);
    if (!fun) fun = find_constrained_interface_method_in_function(program, context_fun, expr->left, NULL);
    if (!fun && expr->left->left) {
      const char *receiver_type = expr->left->left->resolved_type;
      if ((!receiver_type || strcmp(receiver_type, "Unknown") == 0) && scope) receiver_type = expr_type(program, expr->left->left, scope);
      char owner_type[192];
      strip_ref_like_type(receiver_type, owner_type, sizeof(owner_type));
      const Shape *shape = find_shape_for_type(program, owner_type);
      fun = find_shape_method_decl(shape, expr->left->text);
    }
  }
  return fun;
}

static bool expr_call_has_error_flow(const Program *program, const Function *context_fun, Scope *scope, const Expr *expr, size_t depth) {
  if (!expr || expr->kind != EXPR_CALL || !expr->left) return false;
  if (is_builtin_fallible_call(expr) || expr_is_world_stream_write_shape(expr)) return true;
  const Function *fun = resolve_call_function_in_context(program, context_fun, scope, expr);
  return function_has_error_flow_inner(program, fun, depth + 1);
}

static bool expr_has_checked_error_flow(const Program *program, const Function *context_fun, Scope *scope, const Expr *expr, size_t depth) {
  if (!expr) return false;
  if (expr->kind == EXPR_CHECK && expr_call_has_error_flow(program, context_fun, scope, expr->left, depth)) return true;
  if (expr->kind == EXPR_RESCUE) return expr_has_checked_error_flow(program, context_fun, scope, expr->right, depth);
  if (expr_has_checked_error_flow(program, context_fun, scope, expr->left, depth) || expr_has_checked_error_flow(program, context_fun, scope, expr->right, depth)) return true;
  for (size_t i = 0; i < expr->args.len; i++) {
    if (expr_has_checked_error_flow(program, context_fun, scope, expr->args.items[i], depth)) return true;
  }
  for (size_t i = 0; i < expr->fields.len; i++) {
    if (expr_has_checked_error_flow(program, context_fun, scope, expr->fields.items[i].value, depth)) return true;
  }
  return false;
}

static bool stmt_vec_has_checked_error_flow(const Program *program, const Function *context_fun, const StmtVec *body, Scope *scope, size_t depth) {
  if (!body || depth > 64) return false;
  for (size_t i = 0; i < body->len; i++) {
    const Stmt *stmt = body->items[i];
    if (!stmt) continue;
    if (stmt->kind == STMT_RAISE) return true;
    if (stmt->kind == STMT_CHECK && expr_call_has_error_flow(program, context_fun, scope, stmt->expr, depth)) return true;
    if (expr_has_checked_error_flow(program, context_fun, scope, stmt->target, depth) ||
        expr_has_checked_error_flow(program, context_fun, scope, stmt->expr, depth) ||
        expr_has_checked_error_flow(program, context_fun, scope, stmt->range_end, depth)) return true;
    if (stmt->kind == STMT_IF) {
      Scope then_scope = {.parent = scope};
      Scope else_scope = {.parent = scope};
      bool found = stmt_vec_has_checked_error_flow(program, context_fun, &stmt->then_body, &then_scope, depth);
      scope_free(&then_scope);
      if (found) return true;
      found = stmt_vec_has_checked_error_flow(program, context_fun, &stmt->else_body, &else_scope, depth);
      scope_free(&else_scope);
      if (found) return true;
    } else if (stmt->kind == STMT_WHILE) {
      Scope then_scope = {.parent = scope};
      bool found = stmt_vec_has_checked_error_flow(program, context_fun, &stmt->then_body, &then_scope, depth);
      scope_free(&then_scope);
      if (found) return true;
    } else if (stmt->kind == STMT_FOR) {
      Scope body_scope = {.parent = scope};
      const char *iter_type = stmt->resolved_type ? stmt->resolved_type : (stmt->expr ? expr_type(program, stmt->expr, scope) : "Unknown");
      if (stmt->name) scope_add(&body_scope, stmt->name, iter_type ? iter_type : "Unknown", false);
      bool found = stmt_vec_has_checked_error_flow(program, context_fun, &stmt->then_body, &body_scope, depth);
      scope_free(&body_scope);
      if (found) return true;
    } else if (stmt_vec_has_checked_error_flow(program, context_fun, &stmt->then_body, scope, depth)) {
      return true;
    }
    if (stmt->kind != STMT_IF && stmt_vec_has_checked_error_flow(program, context_fun, &stmt->else_body, scope, depth)) return true;
    for (size_t arm_index = 0; arm_index < stmt->match_arms.len; arm_index++) {
      const MatchArm *arm = &stmt->match_arms.items[arm_index];
      if (expr_has_checked_error_flow(program, context_fun, scope, arm->guard, depth)) return true;
      Scope arm_scope = {.parent = scope};
      if (stmt->kind == STMT_MATCH && arm->payload_name && stmt->expr) {
        const char *match_type = stmt->resolved_type ? stmt->resolved_type : expr_type(program, stmt->expr, scope);
        const Choice *item_choice = find_choice(program, match_type);
        const Param *item_case = item_choice ? find_case(&item_choice->cases, arm->case_name) : NULL;
        if (item_case && item_case->type) scope_add(&arm_scope, arm->payload_name, item_case->type, false);
      }
      bool found = stmt_vec_has_checked_error_flow(program, context_fun, &arm->body, &arm_scope, depth);
      scope_free(&arm_scope);
      if (found) return true;
    }
    flow_scope_add_stmt_binding(program, stmt, scope);
  }
  return false;
}

static bool function_has_error_flow_inner(const Program *program, const Function *fun, size_t depth) {
  if (!fun || !fun->raises) return false;
  if (fun->has_error_set) return true;
  Scope flow_scope = {0};
  flow_scope_add_function_bindings(fun, &flow_scope);
  bool result = stmt_vec_has_checked_error_flow(program, fun, &fun->body, &flow_scope, depth);
  scope_free(&flow_scope);
  return result;
}

static bool function_has_error_flow(const Program *program, const Function *fun) {
  return function_has_error_flow_inner(program, fun, 0);
}

static bool check_fallible_call_is_checked(const Program *program, const Function *callee, const Expr *call, ZDiag *diag, const char *message, const char *expected, const char *actual, const char *help) {
  if (!function_has_error_flow(program, callee)) return true;
  if (allow_fallible_call == 0) {
    return set_diag_detail(diag, 1003, message, call->line, call->column, expected, actual, help);
  }
  return true;
}

static const Function *fallible_callee_in_context(const Program *program, const Function *context_fun, Scope *scope, const Expr *expr) {
  const Function *fun = resolve_call_function_in_context(program, context_fun, scope, expr);
  return function_has_error_flow(program, fun) ? fun : NULL;
}

static const Function *fallible_callee(const Program *program, const Expr *expr) {
  return fallible_callee_in_context(program, checking_function, NULL, expr);
}

static bool is_builtin_fallible_call(const Expr *expr) {
  if (!expr || expr->kind != EXPR_CALL || !expr->left || expr->left->kind != EXPR_MEMBER) return false;
  ZBuf name;
  zbuf_init(&name);
  member_name_buf(expr->left, &name);
  bool result = strcmp(name.data, "std.fs.readAllOrRaise") == 0 ||
                strcmp(name.data, "std.fs.openOrRaise") == 0 ||
                strcmp(name.data, "std.fs.createOrRaise") == 0 ||
                strcmp(name.data, "std.fs.readOrRaise") == 0 ||
                strcmp(name.data, "std.fs.writeAllOrRaise") == 0 ||
                strcmp(name.data, "std.fs.fileLenOrRaise") == 0;
  zbuf_free(&name);
  return result;
}

static const char *builtin_fallible_return_type(const Expr *expr) {
  if (!is_builtin_fallible_call(expr)) return NULL;
  ZBuf name;
  zbuf_init(&name);
  member_name_buf(expr->left, &name);
  const char *result = "owned<ByteBuf>";
  if (strcmp(name.data, "std.fs.openOrRaise") == 0 || strcmp(name.data, "std.fs.createOrRaise") == 0) result = "owned<File>";
  else if (strcmp(name.data, "std.fs.readOrRaise") == 0 || strcmp(name.data, "std.fs.fileLenOrRaise") == 0) result = "usize";
  else if (strcmp(name.data, "std.fs.writeAllOrRaise") == 0) result = "Void";
  zbuf_free(&name);
  return result;
}

static bool function_error_sets_include_builtin(const Function *caller, ZDiag *diag, const Expr *call) {
  if (!caller || !caller->raises) {
    return set_diag_detail(diag, 1001, "fallible std call requires function to be marked raises", call->line, call->column, "function signature marked raises", "function is not marked raises", "add raises to the function signature or handle the error locally");
  }
  if (!caller->has_error_set) return true;
  const char *errors[] = {"NotFound", "TooLarge", "Io", NULL};
  for (size_t i = 0; errors[i]; i++) {
    if (!function_error_contains(caller, errors[i])) {
      char actual[160];
      snprintf(actual, sizeof(actual), "std fs call may raise %s", errors[i]);
      return set_diag_detail(diag, 1002, "caller error set does not include std fs error", call->line, call->column, "caller raises set containing NotFound, TooLarge, and Io", actual, "add the missing std fs error to raises { ... } or rescue the call locally");
    }
  }
  return true;
}

static const Shape *find_shape(const Program *program, const char *name) {
  for (size_t i = 0; i < program->shapes.len; i++) {
    if (strcmp(program->shapes.items[i].name, name) == 0) return &program->shapes.items[i];
  }
  return NULL;
}

static const Shape *find_shape_for_type(const Program *program, const char *type) {
  type = resolve_alias_type(type);
  const Shape *shape = find_shape(program, type);
  if (shape) return shape;
  if (!type) return NULL;
  for (size_t i = 0; i < program->shapes.len; i++) {
    const Shape *candidate = &program->shapes.items[i];
    char **args = NULL;
    size_t arg_len = 0;
    bool matched = candidate->type_params.len > 0 && type_generic_arg_list(type, candidate->name, &args, &arg_len) && arg_len == candidate->type_params.len;
    free_type_arg_list(args, arg_len);
    if (matched) return candidate;
  }
  return NULL;
}

static char *shape_field_type_for_owner(const Shape *shape, const char *owner_type, const Param *field) {
  if (!shape || !field) return z_strdup("Unknown");
  owner_type = resolve_alias_type(owner_type);
  if (shape->type_params.len > 0) {
    char **args = NULL;
    size_t arg_len = 0;
    if (type_generic_arg_list(owner_type, shape->name, &args, &arg_len) && arg_len == shape->type_params.len) {
      GenericBinding *bindings = z_checked_calloc(arg_len, sizeof(GenericBinding));
      for (size_t i = 0; i < arg_len; i++) {
        bindings[i].name = shape->type_params.items[i].name;
        bindings[i].type = z_strdup(args[i]);
      }
      char *result = type_substitute_generic(field->type, bindings, arg_len);
      for (size_t i = 0; i < arg_len; i++) free(bindings[i].type);
      free(bindings);
      free_type_arg_list(args, arg_len);
      return result;
    }
    free_type_arg_list(args, arg_len);
  }
  return z_strdup(field->type ? field->type : "Unknown");
}

static const EnumDecl *find_enum(const Program *program, const char *name) {
  for (size_t i = 0; i < program->enums.len; i++) {
    if (strcmp(program->enums.items[i].name, name) == 0) return &program->enums.items[i];
  }
  return NULL;
}

static const Choice *find_choice(const Program *program, const char *name) {
  for (size_t i = 0; i < program->choices.len; i++) {
    if (strcmp(program->choices.items[i].name, name) == 0) return &program->choices.items[i];
  }
  return NULL;
}

static bool has_case(const ParamVec *cases, const char *name) {
  for (size_t i = 0; i < cases->len; i++) {
    if (strcmp(cases->items[i].name, name) == 0) return true;
  }
  return false;
}

static const Param *find_case(const ParamVec *cases, const char *name) {
  for (size_t i = 0; i < cases->len; i++) {
    if (strcmp(cases->items[i].name, name) == 0) return &cases->items[i];
  }
  return NULL;
}

static const Param *find_shape_field(const Shape *shape, const char *name) {
  for (size_t i = 0; i < shape->fields.len; i++) {
    if (strcmp(shape->fields.items[i].name, name) == 0) return &shape->fields.items[i];
  }
  return NULL;
}

static void meta_cache_free(void) {
  MetaCacheEntry *entry = meta_cache;
  while (entry) {
    MetaCacheEntry *next = entry->next;
    free(entry->key);
    free(entry);
    entry = next;
  }
  meta_cache = NULL;
  meta_stats = (ZMetaCacheStats){0};
}

static void meta_expr_key(ZBuf *buf, const Expr *expr) {
  if (!expr) {
    zbuf_append(buf, "<null>");
    return;
  }
  zbuf_appendf(buf, "%d:", (int)expr->kind);
  if (expr->text) zbuf_append(buf, expr->text);
  if (expr->kind == EXPR_BOOL) zbuf_append(buf, expr->bool_value ? "true" : "false");
  if (expr->left) {
    zbuf_append_char(buf, '(');
    meta_expr_key(buf, expr->left);
    zbuf_append_char(buf, ')');
  }
  if (expr->right) {
    zbuf_append_char(buf, '[');
    meta_expr_key(buf, expr->right);
    zbuf_append_char(buf, ']');
  }
  for (size_t i = 0; i < expr->args.len; i++) {
    zbuf_append_char(buf, ',');
    meta_expr_key(buf, expr->args.items[i]);
  }
}

static bool meta_cache_get(const char *key, MetaValue *out) {
  for (MetaCacheEntry *entry = meta_cache; entry; entry = entry->next) {
    if (strcmp(entry->key, key) == 0) {
      if (out) *out = entry->value;
      meta_stats.hits++;
      return true;
    }
  }
  return false;
}

static void meta_cache_put(const char *key, MetaValue value) {
  MetaCacheEntry *entry = z_checked_calloc(1, sizeof(MetaCacheEntry));
  entry->key = z_strdup(key);
  entry->value = value;
  entry->next = meta_cache;
  meta_cache = entry;
  meta_stats.misses++;
  meta_stats.entries++;
}

static bool meta_target_fact(const Expr *expr, MetaValue *out) {
  const ZTargetInfo *target = current_check_target ? current_check_target : z_find_target(z_host_target());
  if (!expr || !target) return false;
  if (expr->kind == EXPR_MEMBER && expr->left && expr->left->kind == EXPR_IDENT && strcmp(expr->left->text, "target") == 0) {
    if (strcmp(expr->text, "os") == 0) {
      *out = (MetaValue){.kind = META_VALUE_STRING};
      snprintf(out->text, sizeof(out->text), "%s", target->os ? target->os : "unknown");
      return true;
    }
    if (strcmp(expr->text, "arch") == 0) {
      *out = (MetaValue){.kind = META_VALUE_STRING};
      snprintf(out->text, sizeof(out->text), "%s", target->arch ? target->arch : "unknown");
      return true;
    }
    if (strcmp(expr->text, "abi") == 0) {
      *out = (MetaValue){.kind = META_VALUE_STRING};
      snprintf(out->text, sizeof(out->text), "%s", target->abi ? target->abi : "none");
      return true;
    }
    if (strcmp(expr->text, "libc") == 0) {
      *out = (MetaValue){.kind = META_VALUE_STRING};
      snprintf(out->text, sizeof(out->text), "%s", target->libc ? target->libc : "none");
      return true;
    }
    if (strcmp(expr->text, "objectFormat") == 0) {
      *out = (MetaValue){.kind = META_VALUE_STRING};
      snprintf(out->text, sizeof(out->text), "%s", target->object_format ? target->object_format : "unknown");
      return true;
    }
    if (strcmp(expr->text, "endian") == 0) {
      *out = (MetaValue){.kind = META_VALUE_STRING};
      snprintf(out->text, sizeof(out->text), "little");
      return true;
    }
    if (strcmp(expr->text, "pointerWidth") == 0) {
      *out = (MetaValue){.kind = META_VALUE_NUMBER, .number = 64};
      return true;
    }
  }
  if (expr->kind == EXPR_CALL && expr->left && expr->left->kind == EXPR_MEMBER &&
      expr->left->left && expr->left->left->kind == EXPR_IDENT &&
      strcmp(expr->left->left->text, "target") == 0 && strcmp(expr->left->text, "hasCapability") == 0 &&
      expr->args.len == 1 && expr->args.items[0]->kind == EXPR_STRING) {
    *out = (MetaValue){.kind = META_VALUE_BOOL, .boolean = z_target_has_capability(target, expr->args.items[0]->text)};
    return true;
  }
  return false;
}

static bool eval_meta_value(const Program *program, const Expr *expr, MetaValue *out, size_t depth, size_t *steps);

static bool meta_type_fact(const Program *program, const Expr *expr, MetaValue *out, size_t depth, size_t *steps) {
  (void)depth;
  (void)steps;
  if (!expr || expr->kind != EXPR_CALL || !expr->left || expr->left->kind != EXPR_IDENT) return false;
  if (strcmp(expr->left->text, "fieldCount") == 0 && expr->args.len == 1 && expr->args.items[0]->kind == EXPR_IDENT) {
    const Shape *shape = find_shape(program, expr->args.items[0]->text);
    if (!shape) return false;
    *out = (MetaValue){.kind = META_VALUE_NUMBER, .number = shape->fields.len};
    return true;
  }
  if (strcmp(expr->left->text, "hasField") == 0 && expr->args.len == 2 &&
      expr->args.items[0]->kind == EXPR_IDENT && expr->args.items[1]->kind == EXPR_STRING) {
    const Shape *shape = find_shape(program, expr->args.items[0]->text);
    if (!shape) return false;
    *out = (MetaValue){.kind = META_VALUE_BOOL, .boolean = find_shape_field(shape, expr->args.items[1]->text) != NULL};
    return true;
  }
  if (strcmp(expr->left->text, "fieldType") == 0 && expr->args.len == 2 &&
      expr->args.items[0]->kind == EXPR_IDENT && expr->args.items[1]->kind == EXPR_STRING) {
    const Shape *shape = find_shape(program, expr->args.items[0]->text);
    const Param *field = shape ? find_shape_field(shape, expr->args.items[1]->text) : NULL;
    if (!field || !field->type) return false;
    *out = (MetaValue){.kind = META_VALUE_STRING};
    snprintf(out->text, sizeof(out->text), "%s", field->type);
    return true;
  }
  if (strcmp(expr->left->text, "enumCaseCount") == 0 && expr->args.len == 1 && expr->args.items[0]->kind == EXPR_IDENT) {
    const EnumDecl *item_enum = find_enum(program, expr->args.items[0]->text);
    if (!item_enum) return false;
    *out = (MetaValue){.kind = META_VALUE_NUMBER, .number = item_enum->cases.len};
    return true;
  }
  if (strcmp(expr->left->text, "hasEnumCase") == 0 && expr->args.len == 2 &&
      expr->args.items[0]->kind == EXPR_IDENT && expr->args.items[1]->kind == EXPR_STRING) {
    const EnumDecl *item_enum = find_enum(program, expr->args.items[0]->text);
    if (!item_enum) return false;
    *out = (MetaValue){.kind = META_VALUE_BOOL, .boolean = has_case(&item_enum->cases, expr->args.items[1]->text)};
    return true;
  }
  if (strcmp(expr->left->text, "choiceCaseCount") == 0 && expr->args.len == 1 && expr->args.items[0]->kind == EXPR_IDENT) {
    const Choice *item_choice = find_choice(program, expr->args.items[0]->text);
    if (!item_choice) return false;
    *out = (MetaValue){.kind = META_VALUE_NUMBER, .number = item_choice->cases.len};
    return true;
  }
  if (strcmp(expr->left->text, "hasChoiceCase") == 0 && expr->args.len == 2 &&
      expr->args.items[0]->kind == EXPR_IDENT && expr->args.items[1]->kind == EXPR_STRING) {
    const Choice *item_choice = find_choice(program, expr->args.items[0]->text);
    if (!item_choice) return false;
    *out = (MetaValue){.kind = META_VALUE_BOOL, .boolean = has_case(&item_choice->cases, expr->args.items[1]->text)};
    return true;
  }
  return false;
}

static bool eval_meta_value_uncached(const Program *program, const Expr *expr, MetaValue *out, size_t depth, size_t *steps) {
  if (!expr || depth > 64 || !steps || ++(*steps) > 1024) return false;
  if (meta_target_fact(expr, out)) return true;
  if (meta_type_fact(program, expr, out, depth, steps)) return true;
  switch (expr->kind) {
    case EXPR_NUMBER: {
      unsigned long long value = 0;
      if (!parse_static_uint_text(expr->text, &value)) return false;
      *out = (MetaValue){.kind = META_VALUE_NUMBER, .number = value};
      return true;
    }
    case EXPR_BOOL:
      *out = (MetaValue){.kind = META_VALUE_BOOL, .boolean = expr->bool_value};
      return true;
    case EXPR_STRING:
      *out = (MetaValue){.kind = META_VALUE_STRING};
      snprintf(out->text, sizeof(out->text), "%s", expr->text ? expr->text : "");
      return true;
    case EXPR_META:
      return eval_meta_value(program, expr->left, out, depth + 1, steps);
    case EXPR_IDENT:
      for (size_t i = 0; program && i < program->consts.len; i++) {
        if (strcmp(program->consts.items[i].name, expr->text) == 0) {
          return eval_meta_value(program, program->consts.items[i].expr, out, depth + 1, steps);
        }
      }
      return false;
    case EXPR_BINARY: {
      MetaValue left = {0};
      MetaValue right = {0};
      if (!eval_meta_value(program, expr->left, &left, depth + 1, steps) ||
          !eval_meta_value(program, expr->right, &right, depth + 1, steps)) return false;
      if (strcmp(expr->text, "==") == 0 || strcmp(expr->text, "!=") == 0) {
        bool equal = false;
        if (left.kind == right.kind && left.kind == META_VALUE_NUMBER) equal = left.number == right.number;
        else if (left.kind == right.kind && left.kind == META_VALUE_BOOL) equal = left.boolean == right.boolean;
        else if (left.kind == right.kind && left.kind == META_VALUE_STRING) equal = strcmp(left.text, right.text) == 0;
        *out = (MetaValue){.kind = META_VALUE_BOOL, .boolean = strcmp(expr->text, "==") == 0 ? equal : !equal};
        return true;
      }
      if (strcmp(expr->text, "&&") == 0 || strcmp(expr->text, "||") == 0) {
        if (left.kind != META_VALUE_BOOL || right.kind != META_VALUE_BOOL) return false;
        *out = (MetaValue){.kind = META_VALUE_BOOL, .boolean = strcmp(expr->text, "&&") == 0 ? (left.boolean && right.boolean) : (left.boolean || right.boolean)};
        return true;
      }
      if (left.kind != META_VALUE_NUMBER || right.kind != META_VALUE_NUMBER) return false;
      if (strcmp(expr->text, "+") == 0) *out = (MetaValue){.kind = META_VALUE_NUMBER, .number = left.number + right.number};
      else if (strcmp(expr->text, "-") == 0) {
        if (right.number > left.number) return false;
        *out = (MetaValue){.kind = META_VALUE_NUMBER, .number = left.number - right.number};
      } else if (strcmp(expr->text, "*") == 0) *out = (MetaValue){.kind = META_VALUE_NUMBER, .number = left.number * right.number};
      else if (strcmp(expr->text, "/") == 0) {
        if (right.number == 0) return false;
        *out = (MetaValue){.kind = META_VALUE_NUMBER, .number = left.number / right.number};
      } else if (strcmp(expr->text, "%") == 0) {
        if (right.number == 0) return false;
        *out = (MetaValue){.kind = META_VALUE_NUMBER, .number = left.number % right.number};
      } else if (strcmp(expr->text, "<") == 0) *out = (MetaValue){.kind = META_VALUE_BOOL, .boolean = left.number < right.number};
      else if (strcmp(expr->text, "<=") == 0) *out = (MetaValue){.kind = META_VALUE_BOOL, .boolean = left.number <= right.number};
      else if (strcmp(expr->text, ">") == 0) *out = (MetaValue){.kind = META_VALUE_BOOL, .boolean = left.number > right.number};
      else if (strcmp(expr->text, ">=") == 0) *out = (MetaValue){.kind = META_VALUE_BOOL, .boolean = left.number >= right.number};
      else return false;
      return true;
    }
    default:
      return false;
  }
}

static bool eval_meta_value(const Program *program, const Expr *expr, MetaValue *out, size_t depth, size_t *steps) {
  ZBuf key;
  zbuf_init(&key);
  zbuf_append(&key, current_check_target ? current_check_target->name : z_host_target());
  zbuf_append_char(&key, ':');
  meta_expr_key(&key, expr);
  if (meta_cache_get(key.data, out)) {
    zbuf_free(&key);
    return true;
  }
  MetaValue value = {0};
  bool ok = eval_meta_value_uncached(program, expr, &value, depth, steps);
  if (ok) {
    meta_cache_put(key.data, value);
    if (out) *out = value;
  }
  zbuf_free(&key);
  return ok;
}

static bool check_meta_expr(const Program *program, const Expr *expr, ZDiag *diag) {
  MetaValue value = {0};
  size_t steps = 0;
  if (!eval_meta_value(program, expr->left, &value, 0, &steps)) {
    return set_diag_detail(diag, 3035, "unsupported meta expression", expr->line, expr->column, "literal arithmetic, target facts, or type facts within safety limits", "sandboxed meta evaluator rejected this expression or hit a safety limit/cycle", "use deterministic meta expressions such as meta 2 + 2, meta target.pointerWidth, or meta hasField(Shape, \"field\")");
  }
  char text[128];
  if (value.kind == META_VALUE_NUMBER) {
    snprintf(text, sizeof(text), "%llu", value.number);
    set_expr_resolved_type(expr, "usize");
    free(((Expr *)expr)->text);
    ((Expr *)expr)->text = z_strdup(text);
  } else if (value.kind == META_VALUE_BOOL) {
    set_expr_resolved_type(expr, "Bool");
    ((Expr *)expr)->bool_value = value.boolean;
    free(((Expr *)expr)->text);
    ((Expr *)expr)->text = z_strdup(value.boolean ? "true" : "false");
  } else {
    set_expr_resolved_type(expr, "String");
    free(((Expr *)expr)->text);
    ((Expr *)expr)->text = z_strdup(value.text);
  }
  return true;
}

static const Function *find_shape_method_decl(const Shape *shape, const char *name) {
  if (!shape || !name) return NULL;
  for (size_t i = 0; i < shape->methods.len; i++) {
    if (strcmp(shape->methods.items[i].name, name) == 0) return &shape->methods.items[i];
  }
  return NULL;
}

static const Function *find_namespace_shape_method(const Program *program, const Expr *callee, const Shape **out_shape) {
  if (!callee || callee->kind != EXPR_MEMBER || !callee->left || callee->left->kind != EXPR_IDENT) return NULL;
  const Shape *shape = find_shape(program, callee->left->text);
  if (!shape) return NULL;
  const Function *method = find_shape_method_decl(shape, callee->text);
  if (method && out_shape) *out_shape = shape;
  return method;
}

static char *shape_open_instance_type(const Shape *shape) {
  if (!shape) return z_strdup("Unknown");
  if (shape->type_params.len == 0) return z_strdup(shape->name);
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, shape->name);
  zbuf_append_char(&buf, '<');
  for (size_t i = 0; i < shape->type_params.len; i++) {
    if (i > 0) zbuf_append_char(&buf, ',');
    zbuf_append(&buf, shape->type_params.items[i].name);
  }
  zbuf_append_char(&buf, '>');
  return buf.data;
}

static char *shape_concrete_instance_type(const Shape *shape, GenericBinding *bindings, size_t binding_len) {
  if (!shape) return z_strdup("Unknown");
  if (shape->type_params.len == 0) return z_strdup(shape->name);
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, shape->name);
  zbuf_append_char(&buf, '<');
  for (size_t i = 0; i < shape->type_params.len; i++) {
    if (i > 0) zbuf_append_char(&buf, ',');
    const char *bound = generic_binding_lookup(bindings, binding_len, shape->type_params.items[i].name);
    zbuf_append(&buf, bound ? bound : "Unknown");
  }
  zbuf_append_char(&buf, '>');
  return buf.data;
}

static void strip_ref_like_type(const char *type, char *out, size_t out_len) {
  if (!out || out_len == 0) return;
  snprintf(out, out_len, "%s", type ? type : "Unknown");
  char inner[160];
  if (owned_inner_text(out, inner, sizeof(inner)) || ref_inner_text(out, inner, sizeof(inner))) {
    snprintf(out, out_len, "%s", inner);
  }
}

static bool infer_shape_method_binding(const char *pattern, const char *actual, GenericBinding *bindings, size_t binding_len) {
  if (!pattern || !actual) return true;
  for (size_t i = 0; i < binding_len; i++) {
    if (strcmp(bindings[i].name, pattern) == 0) return generic_binding_set(bindings, binding_len, pattern, actual);
  }
  const char *generic_names[] = {"Maybe", "Span", "MutSpan", "ref", "mutref", "owned", NULL};
  for (size_t i = 0; generic_names[i]; i++) {
    const char *pattern_inner = NULL;
    const char *actual_inner = NULL;
    size_t pattern_len = 0;
    size_t actual_len = 0;
    if (type_has_generic_arg(pattern, generic_names[i], &pattern_inner, &pattern_len) &&
        type_has_generic_arg(actual, generic_names[i], &actual_inner, &actual_len)) {
      char *pattern_text = z_strndup(pattern_inner, pattern_len);
      char *actual_text = z_strndup(actual_inner, actual_len);
      bool ok = infer_shape_method_binding(pattern_text, actual_text, bindings, binding_len);
      free(pattern_text);
      free(actual_text);
      return ok;
    }
  }
  const char *pattern_open = strchr(pattern, '<');
  const char *actual_open = strchr(actual, '<');
  if (pattern_open && actual_open && pattern_open > pattern && actual_open > actual) {
    size_t name_len = (size_t)(pattern_open - pattern);
    if (strncmp(pattern, actual, name_len) == 0 && actual[name_len] == '<') {
      char *name = z_strndup(pattern, name_len);
      char **pattern_args = NULL;
      char **actual_args = NULL;
      size_t pattern_arg_len = 0;
      size_t actual_arg_len = 0;
      bool ok = type_generic_arg_list(pattern, name, &pattern_args, &pattern_arg_len) &&
                type_generic_arg_list(actual, name, &actual_args, &actual_arg_len) &&
                pattern_arg_len == actual_arg_len;
      for (size_t arg_index = 0; ok && arg_index < pattern_arg_len; arg_index++) {
        ok = infer_shape_method_binding(pattern_args[arg_index], actual_args[arg_index], bindings, binding_len);
      }
      free_type_arg_list(pattern_args, pattern_arg_len);
      free_type_arg_list(actual_args, actual_arg_len);
      free(name);
      return ok;
    }
  }
  return true;
}

static bool bind_shape_params_from_self(const Program *program, const Shape *shape, GenericBinding *bindings, size_t binding_len, ZDiag *diag, const Expr *call) {
  const char *self_type = generic_binding_lookup(bindings, binding_len, "Self");
  if (!self_type) return true;
  char owner_type[192];
  strip_ref_like_type(self_type, owner_type, sizeof(owner_type));
  if (shape->type_params.len == 0) return generic_binding_set(bindings, binding_len, "Self", shape->name);
  char **args = NULL;
  size_t arg_len = 0;
  if (!type_generic_arg_list(resolve_alias_type(owner_type), shape->name, &args, &arg_len) || arg_len != shape->type_params.len) {
    free_type_arg_list(args, arg_len);
    return set_diag_detail(diag, 3047, "shape method Self argument has the wrong instantiation", call->line, call->column, "Self instantiated with the declaring generic shape", owner_type, "call the method with a value of the declaring shape type");
  }
  bool ok = true;
  for (size_t i = 0; i < shape->type_params.len && ok; i++) {
    char *value = shape->type_params.items[i].is_static ? canonical_static_arg_for_type(program, args[i], shape->type_params.items[i].type) : z_strdup(args[i]);
    if (shape->type_params.items[i].is_static && !value) {
      ok = set_diag_detail(diag, 3044, "static value argument must be deterministic and concrete", call->line, call->column, static_value_expected_label(program, shape->type_params.items[i].type), args[i], "pass an explicit literal, top-level const, or supported meta value with the static parameter type");
    } else {
      ok = generic_binding_set(bindings, binding_len, shape->type_params.items[i].name, value);
    }
    free(value);
  }
  free_type_arg_list(args, arg_len);
  if (!ok) return set_diag_detail(diag, 3047, "shape method Self argument conflicts with explicit shape arguments", call->line, call->column, "matching shape method instantiation", owner_type, "use one concrete shape instantiation for the method call");
  return true;
}

static bool shape_method_receiver_info(const Function *method, bool *requires_mut) {
  if (!method || method->params.len == 0 || !method->params.items[0].type) return false;
  char inner[160];
  if (named_ref_inner_text(method->params.items[0].type, "mutref", inner, sizeof(inner)) && strcmp(inner, "Self") == 0) {
    if (requires_mut) *requires_mut = true;
    return true;
  }
  if (named_ref_inner_text(method->params.items[0].type, "ref", inner, sizeof(inner)) && strcmp(inner, "Self") == 0) {
    if (requires_mut) *requires_mut = false;
    return true;
  }
  return false;
}

static char *receiver_self_arg_type(const char *receiver_type, bool requires_mut) {
  char ref_inner[192];
  if (named_ref_inner_text(receiver_type, "ref", ref_inner, sizeof(ref_inner)) ||
      named_ref_inner_text(receiver_type, "mutref", ref_inner, sizeof(ref_inner))) {
    return z_strdup(receiver_type);
  }
  char owner_type[192];
  strip_ref_like_type(receiver_type, owner_type, sizeof(owner_type));
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_appendf(&buf, "%s<%s>", requires_mut ? "mutref" : "ref", owner_type);
  return buf.data;
}

static bool build_receiver_shape_method_bindings(const Program *program, const Shape *shape, const Function *method, const Expr *call, const char *self_arg_type, Scope *scope, ZDiag *diag, GenericBinding **out_bindings, size_t *out_len) {
  size_t binding_len = 1 + (shape ? shape->type_params.len : 0);
  GenericBinding *bindings = z_checked_calloc(binding_len, sizeof(GenericBinding));
  bindings[0].name = "Self";
  for (size_t i = 0; shape && i < shape->type_params.len; i++) bindings[i + 1].name = shape->type_params.items[i].name;
  const TypeArgVec *type_args = call_type_args(call);
  if (type_args && type_args->len > 0) {
    if (!shape || type_args->len != shape->type_params.len) {
      generic_bindings_free(bindings, binding_len);
      free(bindings);
      return set_diag_detail(diag, 3046, "generic receiver method type argument count mismatch", call->line, call->column, "one value/type argument per shape parameter", "wrong receiver method type argument count", "pass explicit arguments matching the shape declaration");
    }
    for (size_t i = 0; i < shape->type_params.len; i++) {
      if (shape->type_params.items[i].is_static) {
        bindings[i + 1].type = canonical_static_arg_for_type(program, type_args->items[i].type, shape->type_params.items[i].type);
        if (!bindings[i + 1].type) {
          generic_bindings_free(bindings, binding_len);
          free(bindings);
          return set_diag_detail(diag, 3044, "static value argument must be deterministic and concrete", call->line, call->column, static_value_expected_label(program, shape->type_params.items[i].type), type_args->items[i].type, "pass an explicit literal, top-level const, or supported meta value with the static parameter type");
        }
      } else {
        bindings[i + 1].type = z_strdup(type_args->items[i].type);
      }
    }
    bindings[0].type = shape_concrete_instance_type(shape, bindings, binding_len);
  }
  if (!infer_shape_method_binding(method->params.items[0].type, self_arg_type, bindings, binding_len)) {
    generic_bindings_free(bindings, binding_len);
    free(bindings);
    return set_diag_detail(diag, 3047, "receiver method Self argument conflicts with explicit shape arguments", call->line, call->column, "matching receiver method instantiation", self_arg_type, "use one concrete shape instantiation for the receiver call");
  }
  for (size_t param_index = 1; method && param_index < method->params.len && param_index - 1 < call->args.len; param_index++) {
    if (!method->params.items[param_index].type || !strstr(method->params.items[param_index].type, "Self")) continue;
    const char *actual = expr_type(program, call->args.items[param_index - 1], scope);
    if (!infer_shape_method_binding(method->params.items[param_index].type, actual, bindings, binding_len)) {
      generic_bindings_free(bindings, binding_len);
      free(bindings);
      return set_diag_detail(diag, 3047, "receiver method argument conflicts with inferred Self type", call->args.items[param_index - 1]->line, call->args.items[param_index - 1]->column, "one concrete shape instantiation", actual, "make all receiver method arguments use the same shape instantiation");
    }
  }
  if (!bind_shape_params_from_self(program, shape, bindings, binding_len, diag, call)) {
    generic_bindings_free(bindings, binding_len);
    free(bindings);
    return false;
  }
  if (!bindings[0].type) bindings[0].type = shape_concrete_instance_type(shape, bindings, binding_len);
  for (size_t i = 0; i < binding_len; i++) {
    if (!bindings[i].type || strcmp(bindings[i].type, "Unknown") == 0) {
      char actual[128];
      snprintf(actual, sizeof(actual), "%s is not bound", bindings[i].name);
      generic_bindings_free(bindings, binding_len);
      free(bindings);
      return set_diag_detail(diag, 3046, "generic receiver method arguments cannot be inferred", call->line, call->column, "explicit shape method arguments or concrete receiver", actual, "call the method on a concrete receiver or pass explicit method type arguments");
    }
  }
  *out_bindings = bindings;
  *out_len = binding_len;
  return true;
}

static bool bind_shape_method_from_expected_self(const Program *program, const Shape *shape, const Function *method, const Expr *call, const char *expected, GenericBinding *bindings, size_t binding_len, ZDiag *diag) {
  if (!program || !shape || !method || !expected || !method->return_type || strcmp(method->return_type, "Self") != 0) return true;
  char **args = NULL;
  size_t arg_len = 0;
  if (!type_generic_arg_list(resolve_alias_type(expected), shape->name, &args, &arg_len) || arg_len != shape->type_params.len) {
    free_type_arg_list(args, arg_len);
    return true;
  }
  for (size_t i = 0; i < shape->type_params.len; i++) {
    if (shape->type_params.items[i].is_static) {
      char *value = canonical_static_arg_for_type(program, args[i], shape->type_params.items[i].type);
      if (!value) {
        free_type_arg_list(args, arg_len);
        return set_diag_detail(diag, 3044, "static value argument must be deterministic and concrete", call->line, call->column, static_value_expected_label(program, shape->type_params.items[i].type), args[i], "pass an explicit literal, top-level const, or supported meta value with the static parameter type");
      }
      if (!generic_binding_set(bindings, binding_len, shape->type_params.items[i].name, value)) {
        free(value);
        free_type_arg_list(args, arg_len);
        return set_diag_detail(diag, 3047, "expected type conflicts with shape method Self type", call->line, call->column, "one concrete shape instantiation", expected, "use explicit shape method arguments when the expected type conflicts");
      }
      free(value);
    } else if (!generic_binding_set(bindings, binding_len, shape->type_params.items[i].name, args[i])) {
      free_type_arg_list(args, arg_len);
      return set_diag_detail(diag, 3047, "expected type conflicts with shape method Self type", call->line, call->column, "one concrete shape instantiation", expected, "use explicit shape method arguments when the expected type conflicts");
    }
  }
  if (!bindings[0].type) bindings[0].type = shape_concrete_instance_type(shape, bindings, binding_len);
  free_type_arg_list(args, arg_len);
  return true;
}

static bool build_shape_method_bindings(const Program *program, const Shape *shape, const Function *method, const Expr *call, Scope *scope, const char *expected, ZDiag *diag, GenericBinding **out_bindings, size_t *out_len) {
  size_t binding_len = 1 + (shape ? shape->type_params.len : 0);
  GenericBinding *bindings = z_checked_calloc(binding_len, sizeof(GenericBinding));
  bindings[0].name = "Self";
  for (size_t i = 0; shape && i < shape->type_params.len; i++) bindings[i + 1].name = shape->type_params.items[i].name;
  const TypeArgVec *type_args = call_type_args(call);
  if (type_args && type_args->len > 0) {
    if (!shape || type_args->len != shape->type_params.len) {
      generic_bindings_free(bindings, binding_len);
      free(bindings);
      return set_diag_detail(diag, 3046, "generic shape method type argument count mismatch", call->line, call->column, "one value/type argument per shape parameter", "wrong generic shape method argument count", "pass explicit arguments matching the shape declaration");
    }
    for (size_t i = 0; i < shape->type_params.len; i++) {
      if (shape->type_params.items[i].is_static) {
        bindings[i + 1].type = canonical_static_arg_for_type(program, type_args->items[i].type, shape->type_params.items[i].type);
        if (!bindings[i + 1].type) {
          generic_bindings_free(bindings, binding_len);
          free(bindings);
          return set_diag_detail(diag, 3044, "static value argument must be deterministic and concrete", call->line, call->column, static_value_expected_label(program, shape->type_params.items[i].type), type_args->items[i].type, "pass an explicit literal, top-level const, or supported meta value with the static parameter type");
        }
      } else {
        bindings[i + 1].type = z_strdup(type_args->items[i].type);
      }
    }
    bindings[0].type = shape_concrete_instance_type(shape, bindings, binding_len);
  }
  if ((!type_args || type_args->len == 0) && !bind_shape_method_from_expected_self(program, shape, method, call, expected, bindings, binding_len, diag)) {
    generic_bindings_free(bindings, binding_len);
    free(bindings);
    return false;
  }
  for (size_t i = 0; method && i < method->params.len && i < call->args.len; i++) {
    if (!method->params.items[i].type || !strstr(method->params.items[i].type, "Self")) continue;
    const char *actual = expr_type(program, call->args.items[i], scope);
    if (!infer_shape_method_binding(method->params.items[i].type, actual, bindings, binding_len)) {
      generic_bindings_free(bindings, binding_len);
      free(bindings);
      return set_diag_detail(diag, 3047, "shape method argument conflicts with inferred Self type", call->args.items[i]->line, call->args.items[i]->column, "one concrete shape instantiation", actual, "make all method arguments use the same shape instantiation");
    }
  }
  if (!bind_shape_params_from_self(program, shape, bindings, binding_len, diag, call)) {
    generic_bindings_free(bindings, binding_len);
    free(bindings);
    return false;
  }
  if (!bindings[0].type) bindings[0].type = shape_concrete_instance_type(shape, bindings, binding_len);
  for (size_t i = 0; i < binding_len; i++) {
    if (!bindings[i].type || strcmp(bindings[i].type, "Unknown") == 0) {
      char actual[128];
      snprintf(actual, sizeof(actual), "%s is not bound", bindings[i].name);
      generic_bindings_free(bindings, binding_len);
      free(bindings);
      return set_diag_detail(diag, 3046, "generic shape method arguments cannot be inferred", call->line, call->column, "explicit shape method arguments or concrete Self argument", actual, "pass a concrete self argument or explicit method type arguments");
    }
  }
  *out_bindings = bindings;
  *out_len = binding_len;
  return true;
}

static const InterfaceDecl *find_interface(const Program *program, const char *name) {
  if (!program || !name) return NULL;
  for (size_t i = 0; i < program->interfaces.len; i++) {
    if (strcmp(program->interfaces.items[i].name, name) == 0) return &program->interfaces.items[i];
  }
  return NULL;
}

static const Function *find_interface_method_decl(const InterfaceDecl *interface, const char *name) {
  if (!interface || !name) return NULL;
  for (size_t i = 0; i < interface->methods.len; i++) {
    if (strcmp(interface->methods.items[i].name, name) == 0) return &interface->methods.items[i];
  }
  return NULL;
}

static bool interface_constraint_parts(const Program *program, const char *constraint, const InterfaceDecl **out_interface, char ***out_args, size_t *out_arg_len) {
  if (out_interface) *out_interface = NULL;
  if (out_args) *out_args = NULL;
  if (out_arg_len) *out_arg_len = 0;
  if (!constraint || strcmp(constraint, "Type") == 0) return true;
  const char *open = strchr(constraint, '<');
  char *name = open ? z_strndup(constraint, (size_t)(open - constraint)) : z_strdup(constraint);
  const InterfaceDecl *interface = find_interface(program, name);
  if (!interface) {
    free(name);
    return false;
  }
  char **args = NULL;
  size_t arg_len = 0;
  bool ok = true;
  if (open) {
    ok = type_generic_arg_list(constraint, name, &args, &arg_len);
  } else {
    arg_len = 0;
  }
  if (ok && arg_len != interface->type_params.len) ok = false;
  free(name);
  if (!ok) {
    free_type_arg_list(args, arg_len);
    return false;
  }
  if (out_interface) *out_interface = interface;
  if (out_args) *out_args = args;
  else free_type_arg_list(args, arg_len);
  if (out_arg_len) *out_arg_len = arg_len;
  return true;
}

static const InterfaceDecl *constrained_interface_for_type_param(const Program *program, const Function *fun, const char *type_param_name, char ***out_args, size_t *out_arg_len) {
  if (out_args) *out_args = NULL;
  if (out_arg_len) *out_arg_len = 0;
  if (!fun || !type_param_name) return NULL;
  for (size_t i = 0; i < fun->type_params.len; i++) {
    Param *param = &fun->type_params.items[i];
    if (strcmp(param->name, type_param_name) != 0 || !param->type || strcmp(param->type, "Type") == 0) continue;
    const InterfaceDecl *interface = NULL;
    char **args = NULL;
    size_t arg_len = 0;
    if (!interface_constraint_parts(program, param->type, &interface, &args, &arg_len)) {
      free_type_arg_list(args, arg_len);
      return NULL;
    }
    if (out_args) *out_args = args;
    else free_type_arg_list(args, arg_len);
    if (out_arg_len) *out_arg_len = arg_len;
    return interface;
  }
  return NULL;
}

static const Function *find_constrained_interface_method_in_function(const Program *program, const Function *fun, const Expr *callee, const InterfaceDecl **out_interface) {
  if (!callee || callee->kind != EXPR_MEMBER || !callee->left || callee->left->kind != EXPR_IDENT) return NULL;
  char **args = NULL;
  size_t arg_len = 0;
  const InterfaceDecl *interface = constrained_interface_for_type_param(program, fun, callee->left->text, &args, &arg_len);
  free_type_arg_list(args, arg_len);
  if (!interface) return NULL;
  const Function *method = find_interface_method_decl(interface, callee->text);
  if (method && out_interface) *out_interface = interface;
  return method;
}

static const Function *find_constrained_interface_method(const Program *program, const Expr *callee, const InterfaceDecl **out_interface) {
  return find_constrained_interface_method_in_function(program, checking_function, callee, out_interface);
}

static const Function *find_concrete_constrained_shape_method_in_context(const Program *program, const Function *context_fun, GenericBinding *bindings, size_t binding_len, const Expr *callee, const Shape **out_shape) {
  if (out_shape) *out_shape = NULL;
  if (!program || !callee || callee->kind != EXPR_MEMBER || !callee->left || callee->left->kind != EXPR_IDENT) return NULL;
  char **constraint_args = NULL;
  size_t constraint_arg_len = 0;
  const InterfaceDecl *interface = constrained_interface_for_type_param(program, context_fun, callee->left->text, &constraint_args, &constraint_arg_len);
  free_type_arg_list(constraint_args, constraint_arg_len);
  if (!interface) return NULL;
  if ((!bindings || binding_len == 0) && return_provenance_expr_bindings && return_provenance_expr_binding_len > 0) {
    bindings = return_provenance_expr_bindings;
    binding_len = return_provenance_expr_binding_len;
  }
  if (!bindings || binding_len == 0) return NULL;
  const char *bound_owner = generic_binding_lookup(bindings, binding_len, callee->left->text);
  if (!bound_owner) return NULL;
  char owner_type[192];
  strip_ref_like_type(bound_owner, owner_type, sizeof(owner_type));
  const Shape *shape = find_shape_for_type(program, owner_type);
  if (!shape) return NULL;
  const Function *method = find_shape_method_decl(shape, callee->text);
  if (method && out_shape) *out_shape = shape;
  return method;
}

static void set_expr_resolved_type(const Expr *expr, const char *type) {
  if (!expr) return;
  Expr *mutable_expr = (Expr *)expr;
  free(mutable_expr->resolved_type);
  mutable_expr->resolved_type = z_strdup(type ? type : "Unknown");
}

static void set_stmt_resolved_type(const Stmt *stmt, const char *type) {
  if (!stmt) return;
  Stmt *mutable_stmt = (Stmt *)stmt;
  free(mutable_stmt->resolved_type);
  mutable_stmt->resolved_type = z_strdup(type ? type : "Unknown");
}

static bool is_bool_type(const char *type) {
  return type && (strcmp(type, "Bool") == 0 || strcmp(type, "bool") == 0);
}

static bool is_char_type(const char *type) {
  return type && strcmp(type, "char") == 0;
}

static bool type_is_const(const char *type) {
  return type && strncmp(type, "const ", strlen("const ")) == 0;
}

static const char *type_strip_const(const char *type) {
  return type_is_const(type) ? type + strlen("const ") : type;
}

static bool is_float_type(const char *type) {
  return type && (strcmp(type, "f32") == 0 || strcmp(type, "f64") == 0);
}

typedef struct {
  const char *name;
  int bits;
  bool is_signed;
  unsigned long long max;
} IntTypeInfo;

static bool int_type_info(const char *type, IntTypeInfo *out) {
  if (!type) return false;
  IntTypeInfo info[] = {
    {"i8", 8, true, 127ull},
    {"i16", 16, true, 32767ull},
    {"i32", 32, true, 2147483647ull},
    {"i64", 64, true, 9223372036854775807ull},
    {"u8", 8, false, 255ull},
    {"u16", 16, false, 65535ull},
    {"u32", 32, false, 4294967295ull},
    {"u64", 64, false, 18446744073709551615ull},
    {"usize", (int)(sizeof(size_t) * 8), false, sizeof(size_t) == 8 ? 18446744073709551615ull : 4294967295ull},
    {"isize", (int)(sizeof(size_t) * 8), true, sizeof(size_t) == 8 ? 9223372036854775807ull : 2147483647ull},
  };
  for (size_t i = 0; i < sizeof(info) / sizeof(info[0]); i++) {
    if (strcmp(type, info[i].name) == 0) {
      if (out) *out = info[i];
      return true;
    }
  }
  return false;
}

static bool is_int_type(const char *type) {
  return int_type_info(type, NULL);
}

static bool is_primitive_cast_type(const char *type) {
  return is_int_type(type) || is_float_type(type) || is_char_type(type);
}

static bool is_abi_scalar_type(const char *type) {
  type = type_strip_const(type);
  return is_int_type(type) || is_float_type(type) || is_char_type(type) || is_bool_type(type) ||
         type_is_named_generic(type, "ref") || type_is_named_generic(type, "mutref");
}

static bool is_c_abi_type(const char *type) {
  type = type_strip_const(type);
  return (type && strcmp(type, "Void") == 0) || is_abi_scalar_type(type);
}

static bool is_packed_scalar_type(const char *type) {
  type = type_strip_const(type);
  return is_int_type(type) || is_char_type(type) || is_bool_type(type);
}

static bool is_float_literal_text(const char *text) {
  return text && strchr(text, '.') != NULL;
}

typedef struct {
  unsigned long long value;
  const char *suffix;
  bool overflow;
} ParsedIntegerLiteral;

static int integer_digit_value(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
  if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
  return -1;
}

static bool parse_integer_literal(const char *text, ParsedIntegerLiteral *out) {
  if (!text || !text[0]) return false;
  size_t text_len = strlen(text);
  size_t body_len = text_len;
  const char *suffix = NULL;
  const char *last_underscore = strrchr(text, '_');
  if (last_underscore) {
    const char *candidate = last_underscore + 1;
    if (candidate[0] == 0) return false;
    if (int_type_info(candidate, NULL)) {
      suffix = candidate;
      body_len = (size_t)(last_underscore - text);
    } else if (candidate[0] == 'i' || candidate[0] == 'u') {
      return false;
    }
  }
  if (body_len == 0) return false;

  int radix = 10;
  size_t cursor_index = 0;
  if (body_len >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    radix = 16;
    cursor_index = 2;
  } else if (body_len >= 2 && text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) {
    radix = 2;
    cursor_index = 2;
  } else if (body_len >= 2 && text[0] == '0' && (text[1] == 'o' || text[1] == 'O')) {
    radix = 8;
    cursor_index = 2;
  }
  if (cursor_index >= body_len) return false;

  unsigned long long value = 0;
  bool saw_digit = false;
  bool previous_underscore = false;
  bool overflow = false;
  for (; cursor_index < body_len; cursor_index++) {
    char ch = text[cursor_index];
    if (ch == '_') {
      if (!saw_digit || previous_underscore) return false;
      previous_underscore = true;
      continue;
    }
    int digit = integer_digit_value(ch);
    if (digit < 0 || digit >= radix) return false;
    if (value > (ULLONG_MAX - (unsigned)digit) / (unsigned)radix) {
      overflow = true;
      value = ULLONG_MAX;
    } else if (!overflow) {
      value = value * (unsigned)radix + (unsigned)digit;
    }
    saw_digit = true;
    previous_underscore = false;
  }
  if (!saw_digit || previous_underscore) return false;
  out->value = value;
  out->suffix = suffix;
  out->overflow = overflow;
  return true;
}

static bool validate_integer_literal_for_type(const Expr *expr, const char *expected, ZDiag *diag) {
  if (!expr || expr->kind != EXPR_NUMBER) return true;
  ParsedIntegerLiteral parsed = {0};
  if (!parse_integer_literal(expr->text, &parsed)) {
    return set_diag_detail(diag, 3021, "malformed integer literal", expr->line, expr->column, "integer literal with optional radix prefix, separators, and integer suffix", expr->text, "use digits valid for the literal radix and a supported integer suffix");
  }
  const char *target = parsed.suffix ? parsed.suffix : (expected && is_int_type(expected) ? expected : "i32");
  IntTypeInfo info;
  int_type_info(target, &info);
  if (parsed.overflow || parsed.value > info.max) {
    char actual[128];
    snprintf(actual, sizeof(actual), "%s overflows %s", expr->text, target);
    return set_diag_detail(diag, 3022, "integer literal is out of range", expr->line, expr->column, target, actual, "use a smaller literal or a wider integer type");
  }
  set_expr_resolved_type(expr, target);
  return true;
}

static bool parse_float_literal(const char *text, double *out, bool *out_of_range) {
  if (!text || !text[0]) return false;
  if (strchr(text, '_')) return false;
  size_t index = 0;
  if (!isdigit((unsigned char)text[index])) return false;
  while (isdigit((unsigned char)text[index])) index++;
  if (text[index] != '.') return false;
  index++;
  if (!isdigit((unsigned char)text[index])) return false;
  while (isdigit((unsigned char)text[index])) index++;
  if (text[index] == 'e' || text[index] == 'E') {
    index++;
    if (text[index] == '+' || text[index] == '-') index++;
    if (!isdigit((unsigned char)text[index])) return false;
    while (isdigit((unsigned char)text[index])) index++;
  }
  if (text[index] != 0) return false;

  errno = 0;
  char *end = NULL;
  double value = strtod(text, &end);
  if (!end || *end != 0) return false;
  *out = value;
  *out_of_range = errno == ERANGE;
  return true;
}

static bool validate_float_literal_for_type(const Expr *expr, const char *expected, ZDiag *diag) {
  if (!expr || expr->kind != EXPR_NUMBER) return true;
  double value = 0.0;
  bool out_of_range = false;
  if (!parse_float_literal(expr->text, &value, &out_of_range)) {
    return set_diag_detail(diag, 3025, "malformed float literal", expr->line, expr->column, "digits '.' digits with optional exponent", expr->text, "use a decimal literal like 1.0, 0.5, or 1.0e-3");
  }
  const char *target = expected && is_float_type(expected) ? expected : "f64";
  if (out_of_range || (strcmp(target, "f32") == 0 && value > FLT_MAX)) {
    char actual[128];
    snprintf(actual, sizeof(actual), "%s overflows %s", expr->text, target);
    return set_diag_detail(diag, 3026, "float literal is out of range", expr->line, expr->column, target, actual, "use a smaller literal or f64");
  }
  set_expr_resolved_type(expr, target);
  return true;
}

static bool fixed_array_type_parts(const char *type, char *length, size_t length_len, char *element, size_t element_len) {
  if (!type || type[0] != '[' || !element || element_len == 0) return false;
  const char *close = strchr(type, ']');
  if (!close || close == type + 1 || !close[1]) return false;
  if (length && length_len > 0) snprintf(length, length_len, "%.*s", (int)(close - type - 1), type + 1);
  snprintf(element, element_len, "%s", close + 1);
  return true;
}

static bool index_element_type(const char *base_type, char *out, size_t out_len) {
  if (!base_type || !out || out_len == 0) return false;
  char ref_inner[128];
  if (ref_inner_text(base_type, ref_inner, sizeof(ref_inner))) base_type = ref_inner;
  if (strcmp(base_type, "String") == 0) {
    snprintf(out, out_len, "u8");
    return true;
  }
  if (fixed_array_type_parts(base_type, NULL, 0, out, out_len)) return true;
  const char *inner = NULL;
  size_t inner_len = 0;
  if (type_has_generic_arg(base_type, "Span", &inner, &inner_len) ||
      type_has_generic_arg(base_type, "MutSpan", &inner, &inner_len)) {
    snprintf(out, out_len, "%.*s", (int)inner_len, inner);
    return true;
  }
  return false;
}

static const char *expr_type(const Program *program, const Expr *expr, Scope *scope) {
  if (!expr) return "Void";
  const char *resolved_type = expr_resolved_type_for_current_context(expr);
  if (resolved_type) return resolved_type;
  switch (expr->kind) {
    case EXPR_STRING: return "String";
    case EXPR_CHAR: return "char";
    case EXPR_NUMBER: {
      if (expr->resolved_type) return expr->resolved_type;
      if (is_float_literal_text(expr->text)) return "f64";
      ParsedIntegerLiteral parsed = {0};
      if (parse_integer_literal(expr->text, &parsed) && parsed.suffix) return parsed.suffix;
      return "i32";
    }
    case EXPR_BOOL: return "Bool";
    case EXPR_NULL: return "Null";
    case EXPR_IDENT: {
      const char *type = scope_type(scope, expr->text);
      if (type) return type;
      const Function *fun = find_function(program, expr->text);
      return fun ? fun->return_type : "Unknown";
    }
    case EXPR_CALL: {
      if (expr->left && expr->left->kind == EXPR_IDENT && strcmp(expr->left->text, "expect") == 0) return "Void";
      if (expr->left && expr->left->kind == EXPR_IDENT) {
        const Function *fun = find_function(program, expr->left->text);
        if (!fun) return "Unknown";
        if (function_is_generic(fun)) {
          GenericBinding *bindings = z_checked_calloc(fun->type_params.len, sizeof(GenericBinding));
          for (size_t i = 0; i < fun->type_params.len; i++) bindings[i].name = fun->type_params.items[i].name;
          ZDiag ignored = {0};
          if (build_generic_bindings(program, fun, expr, scope, &ignored, bindings, fun->type_params.len, NULL)) {
            static char generic_return_type[160];
            char *substituted = type_substitute_generic(fun->return_type, bindings, fun->type_params.len);
            snprintf(generic_return_type, sizeof(generic_return_type), "%s", substituted);
            free(substituted);
            generic_bindings_free(bindings, fun->type_params.len);
            free(bindings);
            return generic_return_type;
          }
          generic_bindings_free(bindings, fun->type_params.len);
          free(bindings);
        }
        return fun->return_type;
      }
      if (expr->left && expr->left->kind == EXPR_MEMBER) {
        if (is_world_stream_write_callee(expr->left, scope)) return "Void";
        const Shape *shape = NULL;
        const Function *method = find_namespace_shape_method(program, expr->left, &shape);
        if (method) {
          GenericBinding *bindings = NULL;
          size_t binding_len = 0;
          ZDiag ignored = {0};
          if (build_shape_method_bindings(program, shape, method, expr, scope, NULL, &ignored, &bindings, &binding_len)) {
            static char method_return_type[160];
            char *substituted = type_substitute_generic(method->return_type, bindings, binding_len);
            snprintf(method_return_type, sizeof(method_return_type), "%s", substituted);
            free(substituted);
            generic_bindings_free(bindings, binding_len);
            free(bindings);
            return method_return_type;
          }
          generic_bindings_free(bindings, binding_len);
          free(bindings);
          return method->return_type ? method->return_type : "Void";
        }
        const InterfaceDecl *interface = NULL;
        const Function *required = find_constrained_interface_method(program, expr->left, &interface);
        if (required && interface) {
          char **constraint_args = NULL;
          size_t constraint_arg_len = 0;
          constrained_interface_for_type_param(program, checking_function, expr->left->left->text, &constraint_args, &constraint_arg_len);
          GenericBinding *interface_bindings = z_checked_calloc(interface->type_params.len, sizeof(GenericBinding));
          for (size_t i = 0; i < interface->type_params.len; i++) {
            interface_bindings[i].name = interface->type_params.items[i].name;
            interface_bindings[i].type = z_strdup(i < constraint_arg_len ? constraint_args[i] : "Unknown");
          }
          static char constrained_return_type[160];
          char *substituted = type_substitute_generic(required->return_type, interface_bindings, interface->type_params.len);
          snprintf(constrained_return_type, sizeof(constrained_return_type), "%s", substituted);
          free(substituted);
          generic_bindings_free(interface_bindings, interface->type_params.len);
          free(interface_bindings);
          free_type_arg_list(constraint_args, constraint_arg_len);
          return constrained_return_type;
        }
        if (expr->left->left && expr->left->left->kind == EXPR_IDENT && find_choice(program, expr->left->left->text)) return expr->left->left->text;
        return std_call_return_type(expr->left);
      }
      return "Unknown";
    }
    case EXPR_INDEX: {
      static char element_type[128];
      if (index_element_type(expr_type(program, expr->left, scope), element_type, sizeof(element_type))) return element_type;
      return "Unknown";
    }
    case EXPR_SLICE: {
      static char slice_type[128];
      char element_type[96];
      if (index_element_type(expr_type(program, expr->left, scope), element_type, sizeof(element_type))) {
        snprintf(slice_type, sizeof(slice_type), "Span<%s>", element_type);
        return slice_type;
      }
      return "Unknown";
    }
    case EXPR_CAST:
      return expr->text ? expr->text : "Unknown";
    case EXPR_BORROW: {
      static char borrow_type[160];
      const char *inner_type = expr_type(program, expr->left, scope);
      char owned_inner[128];
      if (owned_inner_text(inner_type, owned_inner, sizeof(owned_inner))) inner_type = owned_inner;
      snprintf(borrow_type, sizeof(borrow_type), "%s<%s>", expr->mutable_borrow ? "mutref" : "ref", inner_type ? inner_type : "Unknown");
      return borrow_type;
    }
    case EXPR_CHECK: {
      const Function *fun = fallible_callee(program, expr->left);
      if (fun) return fun->return_type;
      const char *builtin_type = builtin_fallible_return_type(expr->left);
      if (builtin_type) return builtin_type;
      const char *checked_type = expr_type(program, expr->left, scope);
      const char *inner = NULL;
      size_t inner_len = 0;
      if (type_has_generic_arg(checked_type, "Maybe", &inner, &inner_len)) {
        static char maybe_inner[128];
        snprintf(maybe_inner, sizeof(maybe_inner), "%.*s", (int)inner_len, inner);
        return maybe_inner;
      }
      return checked_type;
    }
    case EXPR_RESCUE:
      return expr_type(program, expr->right, scope);
    case EXPR_META:
      return expr->resolved_type ? expr->resolved_type : "Unknown";
    case EXPR_SHAPE_LITERAL:
      return find_shape(program, expr->text) ? expr->text : "Unknown";
    case EXPR_ARRAY_LITERAL: {
      static char array_type[128];
      const char *element_type = expr->args.len > 0 ? expr_type(program, expr->args.items[0], scope) : "Unknown";
      snprintf(array_type, sizeof(array_type), "[%zu]%s", expr->args.len, element_type);
      return array_type;
    }
    case EXPR_BINARY:
      if (strcmp(expr->text, "==") == 0 || strcmp(expr->text, "!=") == 0 || strcmp(expr->text, "<") == 0 ||
          strcmp(expr->text, "<=") == 0 || strcmp(expr->text, ">") == 0 || strcmp(expr->text, ">=") == 0 ||
          strcmp(expr->text, "&&") == 0 || strcmp(expr->text, "||") == 0) return "Bool";
      return "i32";
    case EXPR_MEMBER:
      if (expr->left) {
        if (expr->left->kind == EXPR_IDENT && find_enum(program, expr->left->text)) return expr->left->text;
        if (expr->left->kind == EXPR_IDENT && find_choice(program, expr->left->text)) return expr->left->text;
        const char *left_type = expr_type(program, expr->left, scope);
        char owned_shape_type[128];
        char ref_shape_type[128];
        if (owned_inner_text(left_type, owned_shape_type, sizeof(owned_shape_type))) left_type = owned_shape_type;
        if (ref_inner_text(left_type, ref_shape_type, sizeof(ref_shape_type))) left_type = ref_shape_type;
        if (strcmp(left_type, "World") == 0 && (strcmp(expr->text, "out") == 0 || strcmp(expr->text, "err") == 0)) return "WorldStream";
        const Shape *shape = find_shape_for_type(program, left_type);
        if (shape) {
          const Param *field = find_shape_field(shape, expr->text);
          if (field) {
            static char resolved_field_type[160];
            char *field_type = shape_field_type_for_owner(shape, left_type, field);
            snprintf(resolved_field_type, sizeof(resolved_field_type), "%s", field_type);
            free(field_type);
            return resolved_field_type;
          }
          return "Unknown";
        }
        {
          const char *left_type = expr_type(program, expr->left, scope);
          const char *inner = NULL;
          size_t inner_len = 0;
          if (type_has_generic_arg(left_type, "Maybe", &inner, &inner_len)) {
            if (strcmp(expr->text, "has") == 0) return "Bool";
            if (strcmp(expr->text, "value") == 0) {
              static char maybe_value_type[128];
              snprintf(maybe_value_type, sizeof(maybe_value_type), "%.*s", (int)inner_len, inner);
              return maybe_value_type;
            }
          }
        }
      }
      return "Unknown";
  }
  return "Unknown";
}

static bool types_compatible(const char *expected, const char *actual) {
  expected = resolve_alias_type(expected);
  actual = resolve_alias_type(actual);
  if (!expected || strcmp(expected, "Unknown") == 0) return true;
  if (!actual || strcmp(actual, "Unknown") == 0) return true;
  if (strcmp(expected, actual) == 0) return true;
  if (type_is_const(expected)) return types_compatible(type_strip_const(expected), type_strip_const(actual));
  if (type_is_const(actual) && !type_is_const(expected)) return false;
  if (is_int_type(expected) && is_int_type(actual)) return strcmp(expected, actual) == 0;
  if (is_float_type(expected) && is_float_type(actual)) return strcmp(expected, actual) == 0;
  if (type_is_named_generic(expected, "owned")) {
    const char *expected_inner = NULL;
    const char *actual_inner = NULL;
    size_t expected_len = 0;
    size_t actual_len = 0;
    type_has_generic_arg(expected, "owned", &expected_inner, &expected_len);
    bool actual_is_owned = type_has_generic_arg(actual, "owned", &actual_inner, &actual_len);
    char *expected_type = z_strndup(expected_inner, expected_len);
    char *actual_type = actual_is_owned ? z_strndup(actual_inner, actual_len) : z_strdup(actual);
    bool ok = types_compatible(expected_type, actual_type);
    free(expected_type);
    free(actual_type);
    return ok;
  }
  if (type_is_named_generic(expected, "Span") && actual[0] == '[') {
    const char *expected_inner = NULL;
    size_t expected_len = 0;
    const char *actual_close = strchr(actual, ']');
    if (!actual_close || !actual_close[1]) return false;
    type_has_generic_arg(expected, "Span", &expected_inner, &expected_len);
    char *expected_type = z_strndup(expected_inner, expected_len);
    bool ok = types_compatible(expected_type, actual_close + 1);
    free(expected_type);
    return ok;
  }
  if (type_is_named_generic(expected, "Span") && type_is_named_generic(actual, "MutSpan")) {
    const char *expected_inner = NULL;
    const char *actual_inner = NULL;
    size_t expected_len = 0;
    size_t actual_len = 0;
    type_has_generic_arg(expected, "Span", &expected_inner, &expected_len);
    type_has_generic_arg(actual, "MutSpan", &actual_inner, &actual_len);
    char *expected_type = z_strndup(expected_inner, expected_len);
    char *actual_type = z_strndup(actual_inner, actual_len);
    bool ok = types_compatible(expected_type, actual_type);
    free(expected_type);
    free(actual_type);
    return ok;
  }
  if (type_is_named_generic(expected, "ref") && type_is_named_generic(actual, "mutref")) {
    const char *expected_inner = NULL;
    const char *actual_inner = NULL;
    size_t expected_len = 0;
    size_t actual_len = 0;
    type_has_generic_arg(expected, "ref", &expected_inner, &expected_len);
    type_has_generic_arg(actual, "mutref", &actual_inner, &actual_len);
    char *expected_type = z_strndup(expected_inner, expected_len);
    char *actual_type = z_strndup(actual_inner, actual_len);
    bool ok = types_compatible(expected_type, actual_type);
    free(expected_type);
    free(actual_type);
    return ok;
  }
  for (size_t i = 0; i < 5; i++) {
    const char *name = i == 0 ? "Span" : (i == 1 ? "MutSpan" : (i == 2 ? "Maybe" : (i == 3 ? "ref" : "mutref")));
    if (type_is_named_generic(expected, name) && type_is_named_generic(actual, name)) {
      const char *expected_inner = NULL;
      const char *actual_inner = NULL;
      size_t expected_len = 0;
      size_t actual_len = 0;
      type_has_generic_arg(expected, name, &expected_inner, &expected_len);
      type_has_generic_arg(actual, name, &actual_inner, &actual_len);
      char *expected_type = z_strndup(expected_inner, expected_len);
      char *actual_type = z_strndup(actual_inner, actual_len);
      bool ok = types_compatible(expected_type, actual_type);
      free(expected_type);
      free(actual_type);
      return ok;
    }
  }
  if (expected[0] == '[' && actual[0] == '[') {
    const char *expected_close = strchr(expected, ']');
    const char *actual_close = strchr(actual, ']');
    if (!expected_close || !actual_close) return false;
    char *expected_length = z_strndup(expected + 1, (size_t)(expected_close - expected - 1));
    char *actual_length = z_strndup(actual + 1, (size_t)(actual_close - actual - 1));
    char *expected_static = canonical_static_arg(current_type_program, expected_length);
    char *actual_static = canonical_static_arg(current_type_program, actual_length);
    bool same_length = strcmp(expected_static ? expected_static : expected_length, actual_static ? actual_static : actual_length) == 0;
    free(expected_length);
    free(actual_length);
    free(expected_static);
    free(actual_static);
    if (!same_length) return false;
    return types_compatible(expected_close + 1, actual_close + 1);
  }
  const char *expected_open = strchr(expected, '<');
  const char *actual_open = strchr(actual, '<');
  if (expected_open && actual_open && expected_open > expected && actual_open > actual) {
    size_t name_len = (size_t)(expected_open - expected);
    if (name_len == (size_t)(actual_open - actual) && strncmp(expected, actual, name_len) == 0) {
      char *name = z_strndup(expected, name_len);
      char **expected_args = NULL;
      char **actual_args = NULL;
      size_t expected_arg_len = 0;
      size_t actual_arg_len = 0;
      bool ok = type_generic_arg_list(expected, name, &expected_args, &expected_arg_len) &&
                type_generic_arg_list(actual, name, &actual_args, &actual_arg_len) &&
                expected_arg_len == actual_arg_len;
      for (size_t i = 0; ok && i < expected_arg_len; i++) {
        char *expected_static = canonical_static_arg(current_type_program, expected_args[i]);
        char *actual_static = canonical_static_arg(current_type_program, actual_args[i]);
        if (expected_static || actual_static) {
          ok = expected_static && actual_static && strcmp(expected_static, actual_static) == 0;
        } else {
          ok = types_compatible(expected_args[i], actual_args[i]);
        }
        free(expected_static);
        free(actual_static);
      }
      free_type_arg_list(expected_args, expected_arg_len);
      free_type_arg_list(actual_args, actual_arg_len);
      free(name);
      return ok;
    }
  }
  return false;
}

static bool type_static_value_mismatch(const Program *program, const char *expected, const char *actual) {
  expected = resolve_alias_type(expected);
  actual = resolve_alias_type(actual);
  if (!expected || !actual) return false;
  const char *wrappers[] = {"ref", "mutref", "owned", "Span", "MutSpan", "Maybe", NULL};
  for (size_t i = 0; wrappers[i]; i++) {
    const char *expected_inner = NULL;
    const char *actual_inner = NULL;
    size_t expected_len = 0;
    size_t actual_len = 0;
    if (type_has_generic_arg(expected, wrappers[i], &expected_inner, &expected_len) &&
        type_has_generic_arg(actual, wrappers[i], &actual_inner, &actual_len)) {
      char *expected_text = z_strndup(expected_inner, expected_len);
      char *actual_text = z_strndup(actual_inner, actual_len);
      bool mismatch = type_static_value_mismatch(program, expected_text, actual_text);
      free(expected_text);
      free(actual_text);
      return mismatch;
    }
  }
  if (expected[0] == '[' && actual[0] == '[') {
    const char *expected_close = strchr(expected, ']');
    const char *actual_close = strchr(actual, ']');
    if (!expected_close || !actual_close) return false;
    char *expected_length = z_strndup(expected + 1, (size_t)(expected_close - expected - 1));
    char *actual_length = z_strndup(actual + 1, (size_t)(actual_close - actual - 1));
    char *expected_static = canonical_static_arg(program, expected_length);
    char *actual_static = canonical_static_arg(program, actual_length);
    bool mismatch = expected_static && actual_static && strcmp(expected_static, actual_static) != 0;
    free(expected_length);
    free(actual_length);
    free(expected_static);
    free(actual_static);
    return mismatch;
  }
  const char *expected_open = strchr(expected, '<');
  const char *actual_open = strchr(actual, '<');
  if (!expected_open || !actual_open || expected_open == expected || actual_open == actual) return false;
  size_t name_len = (size_t)(expected_open - expected);
  if (name_len != (size_t)(actual_open - actual) || strncmp(expected, actual, name_len) != 0) return false;
  char *name = z_strndup(expected, name_len);
  const Shape *shape = find_shape(program, name);
  char **expected_args = NULL;
  char **actual_args = NULL;
  size_t expected_arg_len = 0;
  size_t actual_arg_len = 0;
  bool mismatch = false;
  if (shape && type_generic_arg_list(expected, name, &expected_args, &expected_arg_len) &&
      type_generic_arg_list(actual, name, &actual_args, &actual_arg_len) &&
      expected_arg_len == actual_arg_len && expected_arg_len == shape->type_params.len) {
    for (size_t i = 0; i < expected_arg_len; i++) {
      if (!shape->type_params.items[i].is_static) continue;
      char *expected_static = canonical_static_arg(program, expected_args[i]);
      char *actual_static = canonical_static_arg(program, actual_args[i]);
      if (expected_static && actual_static && strcmp(expected_static, actual_static) != 0) mismatch = true;
      free(expected_static);
      free(actual_static);
    }
  }
  free_type_arg_list(expected_args, expected_arg_len);
  free_type_arg_list(actual_args, actual_arg_len);
  free(name);
  return mismatch;
}

static bool check_call_callee(const Program *program, const Expr *callee, Scope *scope, ZDiag *diag) {
  if (callee->kind == EXPR_IDENT) {
    if (scope_has(scope, callee->text)) {
      const char *actual = expr_type(program, callee, scope);
      return set_diag_detail(diag, 3005, "call target is not a function", callee->line, callee->column, "function name", actual, "call a declared function instead of a local value");
    }
    const Function *fun = find_function(program, callee->text);
    if (fun) return true;
    return check_expr(program, callee, scope, diag);
  }
  if (callee->kind == EXPR_MEMBER && find_namespace_shape_method(program, callee, NULL)) return true;
  if (callee->kind == EXPR_MEMBER && find_constrained_interface_method(program, callee, NULL)) return true;
  if (callee->kind == EXPR_MEMBER) {
    ZBuf name;
    zbuf_init(&name);
    member_name_buf(callee, &name);
    bool known_builtin_call = std_call_arg_count(name.data) >= 0;
    bool std_namespace = strncmp(name.data, "std.", strlen("std.")) == 0;
    zbuf_free(&name);
    if (known_builtin_call || std_namespace || is_world_stream_write_callee(callee, scope)) return true;
    return check_expr(program, callee, scope, diag);
  }
  return check_expr(program, callee, scope, diag);
}

static bool check_expr_expected(const Program *program, const Expr *expr, Scope *scope, ZDiag *diag, const char *expected) {
  if (!expr) return true;
  if (expr->kind == EXPR_NUMBER) {
    if (is_float_literal_text(expr->text)) return validate_float_literal_for_type(expr, expected, diag);
    return validate_integer_literal_for_type(expr, expected, diag);
  }
  if (expr->kind == EXPR_NULL) {
    if (expected && type_is_named_generic(expected, "Maybe")) {
      set_expr_resolved_type(expr, expected);
      return true;
    }
    return set_diag_detail(diag, 3017, "null requires an explicit Maybe<T> context", expr->line, expr->column, "Maybe<T>", expected ? expected : "untyped null", "add a Maybe<T> annotation or use a non-null value");
  }
  switch (expr->kind) {
    case EXPR_IDENT:
      if (!scope_has(scope, expr->text) && !function_exists(program, expr->text) && !is_builtin_value(expr->text)) {
        char message[256];
        snprintf(message, sizeof(message), "unknown identifier '%s'", expr->text);
        return set_diag_detail(diag, 3003, message, expr->line, expr->column, "visible local, parameter, function, or builtin", "no matching visible symbol", "declare the name before using it");
      }
      if (function_exists(program, expr->text) && !scope_has(scope, expr->text)) {
        char message[256];
        snprintf(message, sizeof(message), "function '%s' cannot be used as a runtime value", expr->text);
        return set_diag_detail(diag, 3005, message, expr->line, expr->column, "function call expression", "function name", "call the function with parentheses");
      }
      if (is_builtin_value(expr->text) && !scope_has(scope, expr->text)) {
        char message[256];
        snprintf(message, sizeof(message), "builtin namespace '%s' cannot be used as a runtime value", expr->text);
        return set_diag_detail(diag, 3005, message, expr->line, expr->column, "runtime value", "builtin namespace", "use a supported member call such as std.fs.host()");
      }
      {
        const char *actual = scope_type(scope, expr->text);
        if (actual && strcmp(actual, "Type") == 0) {
          char message[256];
          snprintf(message, sizeof(message), "type name '%s' cannot be used as a runtime value", expr->text);
          return set_diag_detail(diag, 3005, message, expr->line, expr->column, "runtime value", "type name", "use the type name in an annotation or constructor context");
        }
        if (actual && type_is_owned(actual) && scope_is_moved(scope, expr->text)) {
          char actual_detail[160];
          snprintf(actual_detail, sizeof(actual_detail), "%s was moved", expr->text);
          return set_diag_detail(diag, 3013, "owned value was already moved", expr->line, expr->column, "live owned binding", actual_detail, "stop using the old binding after transferring ownership");
        }
        if (!check_read_not_mutably_borrowed(expr, scope, diag)) return false;
      }
      if (expected && type_is_named_generic(expected, "MutSpan")) {
        const char *actual = expr_type(program, expr, scope);
        char expected_element[128];
        char actual_element[128];
        if (mutspan_element_text(expected, expected_element, sizeof(expected_element)) &&
            fixed_array_type_parts(actual, NULL, 0, actual_element, sizeof(actual_element))) {
          if (!scope_is_mutable(scope, expr->text)) {
            return set_diag_detail(diag, 3010, "cannot create MutSpan from immutable array binding", expr->line, expr->column, "array binding declared with let mut", "immutable array binding", "add mut to the array binding before creating a MutSpan");
          }
          if (!types_compatible(expected_element, actual_element)) {
            return set_diag_detail(diag, 3006, "MutSpan element type does not match array element", expr->line, expr->column, expected_element, actual_element, "use a MutSpan with the same element type as the array");
          }
          set_expr_resolved_type(expr, expected);
          return true;
        }
      }
      if (!expr->resolved_type) set_expr_resolved_type(expr, expr_type(program, expr, scope));
      return true;
    case EXPR_MEMBER:
      if (expr->left && expr->left->kind == EXPR_IDENT) {
        const EnumDecl *item_enum = find_enum(program, expr->left->text);
        if (item_enum) {
          if (!has_case(&item_enum->cases, expr->text)) return set_diag_detail(diag, 3103, "unknown enum case", expr->line, expr->column, "declared enum case", expr->text, "rename the case or update the enum");
          return true;
        }
        const Choice *item_choice = find_choice(program, expr->left->text);
        if (item_choice) {
          if (!has_case(&item_choice->cases, expr->text)) return set_diag_detail(diag, 3104, "unknown choice case", expr->line, expr->column, "declared choice case", expr->text, "rename the case or update the choice");
          return true;
        }
      }
      char root[128];
      char path[256];
      bool local_place = expr_binding_path(expr, root, sizeof(root), path, sizeof(path)) && scope_has(scope, root);
      if (local_place) {
        if (!check_place_root_available(expr, scope, root, diag)) return false;
        if (!check_place_index_exprs(program, expr, scope, diag)) return false;
      } else if (!check_expr(program, expr->left, scope, diag)) return false;
      {
        const char *left_type = expr_type(program, expr->left, scope);
        char owned_shape_type[128];
        char ref_shape_type[128];
        if (owned_inner_text(left_type, owned_shape_type, sizeof(owned_shape_type))) left_type = owned_shape_type;
        if (ref_inner_text(left_type, ref_shape_type, sizeof(ref_shape_type))) left_type = ref_shape_type;
        if (strcmp(left_type, "World") == 0 && (strcmp(expr->text, "out") == 0 || strcmp(expr->text, "err") == 0)) {
          return set_diag_detail(diag, 3005, "World stream member cannot be used as a runtime value", expr->line, expr->column, "world.out.write(...) or world.err.write(...)", expr->text, "call write directly on the stream capability");
        }
        const Shape *shape = find_shape_for_type(program, left_type);
        if (shape) {
          if (!find_shape_field(shape, expr->text)) {
            char message[256];
            snprintf(message, sizeof(message), "shape '%s' has no field '%s'", shape->name, expr->text);
            return set_diag_detail(diag, 3101, message, expr->line, expr->column, "declared shape field", expr->text, "rename the field or update the shape");
          }
          if (!expr->resolved_type) set_expr_resolved_type(expr, expr_type(program, expr, scope));
          if (!check_read_not_mutably_borrowed(expr, scope, diag)) return false;
          return true;
        }
        {
          const char *inner = NULL;
          size_t inner_len = 0;
          if (type_has_generic_arg(left_type, "Maybe", &inner, &inner_len)) {
            if (strcmp(expr->text, "has") == 0 || strcmp(expr->text, "value") == 0) {
              if (!expr->resolved_type) set_expr_resolved_type(expr, expr_type(program, expr, scope));
              if (!check_read_not_mutably_borrowed(expr, scope, diag)) return false;
              return true;
            }
            return set_diag_detail(diag, 3101, "unknown Maybe member", expr->line, expr->column, "has or value", expr->text, "use .has to test a Maybe or .value after proving it is present");
          }
        }
        return set_diag_detail(diag, 3027, "member access requires a shape, Maybe value, enum case, or choice case", expr->line, expr->column, "declared field or variant case", left_type, "remove the member access or use a value with declared members");
      }
    case EXPR_INDEX: {
      char root[128];
      char path[256];
      bool local_place = expr_binding_path(expr, root, sizeof(root), path, sizeof(path)) && scope_has(scope, root);
      if (local_place) {
        if (!check_place_root_available(expr, scope, root, diag)) return false;
        if (!check_place_index_exprs(program, expr, scope, diag)) return false;
      } else if (!check_expr(program, expr->left, scope, diag)) return false;
      const char *base_type = expr_type(program, expr->left, scope);
      char element_type[128];
      if (!index_element_type(base_type, element_type, sizeof(element_type))) {
        return set_diag_detail(diag, 3027, "value does not support indexing", expr->line, expr->column, "[N]T, Span<T>, or String", base_type, "index fixed arrays, Span<T>, or byte-oriented String values supported by this compiler");
      }
      if (!local_place) {
        if (!check_expr_expected(program, expr->right, scope, diag, "usize")) return false;
        const char *index_type = expr_type(program, expr->right, scope);
        if (!is_int_type(index_type)) {
          return set_diag_detail(diag, 3028, "index expression must be an integer", expr->right ? expr->right->line : expr->line, expr->right ? expr->right->column : expr->column, "integer index", index_type, "use an integer expression such as usize or a checked integer literal");
        }
      }
      set_expr_resolved_type(expr, element_type);
      if (!check_read_not_mutably_borrowed(expr, scope, diag)) return false;
      return true;
    }
    case EXPR_BORROW: {
      if (!expr_is_addressable(expr->left)) {
        return set_diag_detail(diag, 3029, "borrow requires an addressable local lvalue", expr->line, expr->column, expr->mutable_borrow ? "&mut lvalue" : "&lvalue", "temporary or unsupported expression", "borrow a local binding, field, or indexed lvalue");
      }
      char root[128];
      char path[256];
      if (!expr_binding_path(expr->left, root, sizeof(root), path, sizeof(path)) || !scope_has(scope, root)) {
        return set_diag_detail(diag, 3003, "borrow target is not a visible local", expr->line, expr->column, "visible local binding", "unknown borrow root", "borrow a local binding that is in scope");
      }
      if (expr->mutable_borrow) {
        char target_type[128];
        if (!check_lvalue_target(program, expr->left, scope, diag, target_type, sizeof(target_type))) return false;
      } else {
        if (!check_expr(program, expr->left, scope, diag)) return false;
      }
      if (!check_borrow_conflict_at(scope, root, path, expr->mutable_borrow, diag, expr)) return false;
      const char *inner_type = expr_type(program, expr->left, scope);
      char owned_inner[128];
      if (owned_inner_text(inner_type, owned_inner, sizeof(owned_inner))) inner_type = owned_inner;
      char borrow_type[160];
      snprintf(borrow_type, sizeof(borrow_type), "%s<%s>", expr->mutable_borrow ? "mutref" : "ref", inner_type);
      set_expr_resolved_type(expr, borrow_type);
      return true;
    }
    case EXPR_CHECK: {
      if (!checking_function || !checking_function->raises) {
        return set_diag_detail(diag, 1001, "`check` requires function to be marked raises", expr->line, expr->column, "function signature marked raises", "function is not marked raises", "add raises to the function signature");
      }
      allow_fallible_call++;
      bool checked_ok = check_expr(program, expr->left, scope, diag);
      allow_fallible_call--;
      if (!checked_ok) return false;
      const Function *callee = fallible_callee(program, expr->left);
      if (callee && !function_error_sets_compatible(checking_function, callee, diag, expr->left)) return false;
      if (is_builtin_fallible_call(expr->left) && !function_error_sets_include_builtin(checking_function, diag, expr->left)) return false;
      const char *checked_type = expr_type(program, expr->left, scope);
      if (callee) {
        set_expr_resolved_type(expr, callee->return_type);
        return true;
      }
      const char *builtin_type = builtin_fallible_return_type(expr->left);
      if (builtin_type) {
        set_expr_resolved_type(expr, builtin_type);
        return true;
      }
      if (is_world_stream_write_call(expr->left, scope)) {
        set_expr_resolved_type(expr, "Void");
        return true;
      }
      const char *inner = NULL;
      size_t inner_len = 0;
      if (!type_has_generic_arg(checked_type, "Maybe", &inner, &inner_len)) {
        return set_diag_detail(diag, 1001, "`check` expects Maybe<T> or a fallible function call", expr->line, expr->column, "Maybe<T> or raises { ... } call", checked_type, "check a Maybe value or a named-error fallible function");
      }
      char value_type[128];
      snprintf(value_type, sizeof(value_type), "%.*s", (int)inner_len, inner);
      set_expr_resolved_type(expr, value_type);
      return true;
    }
    case EXPR_RESCUE: {
      allow_fallible_call++;
      bool left_ok = check_expr(program, expr->left, scope, diag);
      allow_fallible_call--;
      if (!left_ok) return false;
      const Function *callee = fallible_callee(program, expr->left);
      const char *left_type = callee ? callee->return_type : builtin_fallible_return_type(expr->left);
      if (!left_type) {
        const char *maybe_type = expr_type(program, expr->left, scope);
        const char *inner = NULL;
        size_t inner_len = 0;
        if (!type_has_generic_arg(maybe_type, "Maybe", &inner, &inner_len)) {
          return set_diag_detail(diag, 1001, "rescue expects Maybe<T> or a fallible function call", expr->line, expr->column, "fallible expression rescue err { fallback }", maybe_type, "rescue a Maybe value, user fallible call, or std fallible call");
        }
        char inner_type[128];
        snprintf(inner_type, sizeof(inner_type), "%.*s", (int)inner_len, inner);
        left_type = inner_type;
        ProvenanceScopeSnapshot *before_right = provenance_scope_snapshot_capture(scope);
        if (!check_expr_expected(program, expr->right, scope, diag, left_type)) {
          provenance_scope_snapshot_restore(before_right);
          provenance_scope_snapshot_free(before_right);
          return false;
        }
        ProvenanceScopeSnapshot *right_after = provenance_scope_snapshot_capture(scope);
        provenance_scope_snapshot_restore_optional_branch(before_right, right_after, true, true);
        provenance_scope_snapshot_free(right_after);
        provenance_scope_snapshot_free(before_right);
        set_expr_resolved_type(expr, left_type);
        return true;
      }
      ProvenanceScopeSnapshot *before_right = provenance_scope_snapshot_capture(scope);
      if (!check_expr_expected(program, expr->right, scope, diag, left_type)) {
        provenance_scope_snapshot_restore(before_right);
        provenance_scope_snapshot_free(before_right);
        return false;
      }
      ProvenanceScopeSnapshot *right_after = provenance_scope_snapshot_capture(scope);
      provenance_scope_snapshot_restore_optional_branch(before_right, right_after, true, true);
      provenance_scope_snapshot_free(right_after);
      provenance_scope_snapshot_free(before_right);
      const char *fallback_type = expr_type(program, expr->right, scope);
      if (!types_compatible(left_type, fallback_type)) {
        return set_diag_detail(diag, 3006, "rescue fallback type does not match recovered value", expr->line, expr->column, left_type, fallback_type, "return a fallback value with the same type as the fallible expression");
      }
      set_expr_resolved_type(expr, left_type);
      return true;
    }
    case EXPR_META:
      return check_meta_expr(program, expr, diag);
    case EXPR_SLICE: {
      if (!check_expr(program, expr->left, scope, diag)) return false;
      const char *base_type = expr_type(program, expr->left, scope);
      char element_type[128];
      if (!index_element_type(base_type, element_type, sizeof(element_type))) {
        return set_diag_detail(diag, 3027, "value does not support slicing", expr->line, expr->column, "[N]T, Span<T>, or String", base_type, "slice fixed arrays, Span<T>, or byte-oriented String values supported by this compiler");
      }
      if (expr->args.len != 2) return set_diag_detail(diag, 100, "malformed slice expression", expr->line, expr->column, "start..end, start.., ..end, or ..", "invalid slice", "use valid slice syntax");
      for (size_t i = 0; i < expr->args.len; i++) {
        Expr *bound = expr->args.items[i];
        if (!bound) continue;
        if (!check_expr_expected(program, bound, scope, diag, "usize")) return false;
        const char *bound_type = expr_type(program, bound, scope);
        if (!is_int_type(bound_type)) {
          return set_diag_detail(diag, 3028, "slice bounds must be integers", bound->line, bound->column, "integer slice bound", bound_type, "use integer expressions for slice start and end");
        }
      }
      char slice_type[160];
      snprintf(slice_type, sizeof(slice_type), "Span<%s>", element_type);
      set_expr_resolved_type(expr, slice_type);
      return true;
    }
    case EXPR_CALL:
      if (expr->left && expr->left->kind == EXPR_IDENT && strcmp(expr->left->text, "expect") == 0) {
        if (expr->args.len != 1) return set_diag_detail(diag, 3004, "expect requires one Bool argument", expr->line, expr->column, "expect(condition)", "wrong argument count", "pass exactly one Bool condition");
        if (!check_expr_expected(program, expr->args.items[0], scope, diag, "Bool")) return false;
        const char *actual = expr_type(program, expr->args.items[0], scope);
        if (!is_bool_type(actual)) return set_diag_detail(diag, 3005, "expect argument must be Bool", expr->args.items[0]->line, expr->args.items[0]->column, "Bool", actual, "compare explicitly before calling expect");
        set_expr_resolved_type(expr, "Void");
        return true;
      }
      if (expr->left && expr->left->kind == EXPR_MEMBER && strcmp(expr->left->text, "drop") == 0) {
        return set_diag_detail(diag, 3014, "drop methods are specified but not enforced by this compiler", expr->line, expr->column, "simple defer cleanup expression", "drop method call", "use an explicit cleanup function with defer until owned drop checking lands");
      }
      if (expr->left && expr->left->kind == EXPR_MEMBER && expr->left->left && expr->left->left->kind == EXPR_IDENT) {
        const Choice *choice = find_choice(program, expr->left->left->text);
        if (choice) {
          const Param *item_case = find_case(&choice->cases, expr->left->text);
          if (!item_case) return set_diag_detail(diag, 3104, "unknown choice case", expr->line, expr->column, "declared choice case", expr->left->text, "rename the case or update the choice");
          size_t expected_count = item_case->type ? 1 : 0;
          if (expr->args.len != expected_count) return set_diag_detail(diag, 3108, "choice payload arity mismatch", expr->line, expr->column, expected_count ? "one payload argument" : "no payload arguments", "wrong payload argument count", "add or remove the payload argument");
          for (size_t i = 0; i < expr->args.len; i++) {
            if (!check_expr_expected(program, expr->args.items[i], scope, diag, item_case->type)) return false;
            mark_owned_move_if_needed(expr->args.items[i], scope, item_case->type);
          }
          if (item_case->type && expr->args.len == 1) {
            const char *actual = expr_type(program, expr->args.items[0], scope);
            if (!types_compatible(item_case->type, actual)) return set_diag_detail(diag, 3109, "choice payload type mismatch", expr->args.items[0]->line, expr->args.items[0]->column, item_case->type, actual, "pass a payload value of the declared case type");
          }
          return true;
        }
      }
      if (expr->left && expr->left->kind == EXPR_MEMBER) {
        const Shape *shape = NULL;
        const Function *method = find_namespace_shape_method(program, expr->left, &shape);
        if (method) {
          if (method->params.len != expr->args.len) {
            char message[256];
            snprintf(message, sizeof(message), "method '%s.%s' expects %zu argument(s), got %zu", shape->name, method->name, method->params.len, expr->args.len);
            return set_diag_detail(diag, 3004, message, expr->line, expr->column, "matching method argument count", "wrong argument count", "update the static method call or method signature");
          }
          GenericBinding *method_bindings = NULL;
          size_t method_binding_len = 0;
          if (!build_shape_method_bindings(program, shape, method, expr, scope, expected, diag, &method_bindings, &method_binding_len)) return false;
          for (size_t i = 0; i < expr->args.len; i++) {
            char *expected_type = type_substitute_generic(method->params.items[i].type, method_bindings, method_binding_len);
            if (!check_expr_expected(program, expr->args.items[i], scope, diag, expected_type)) {
              free(expected_type);
              generic_bindings_free(method_bindings, method_binding_len);
              free(method_bindings);
              return false;
            }
            const char *actual = expr_type(program, expr->args.items[i], scope);
            if (!types_compatible(expected_type, actual)) {
              char message[256];
              snprintf(message, sizeof(message), "argument %zu to method '%s.%s' has incompatible type", i + 1, shape->name, method->name);
              bool ok = type_static_value_mismatch(program, expected_type, actual)
                ? set_diag_detail(diag, 3047, "shape method static value does not match Self instantiation", expr->args.items[i]->line, expr->args.items[i]->column, expected_type, actual, "call the method with one matching generic shape instantiation")
                : set_diag_detail(diag, 3005, message, expr->args.items[i]->line, expr->args.items[i]->column, expected_type, actual, "pass a value matching the static method parameter");
              free(expected_type);
              generic_bindings_free(method_bindings, method_binding_len);
              free(method_bindings);
              return ok;
            }
            mark_owned_move_if_needed(expr->args.items[i], scope, expected_type);
            free(expected_type);
          }
          if (function_has_error_flow(program, method)) {
            char actual[160];
            snprintf(actual, sizeof(actual), "call to '%s.%s'", shape->name, method->name);
            if (!check_fallible_call_is_checked(program, method, expr, diag, "fallible static method call must be checked", "check Shape.method(...)", actual, "prefix the call with check in a function marked raises")) {
              generic_bindings_free(method_bindings, method_binding_len);
              free(method_bindings);
              return false;
            }
          }
          char *return_type = type_substitute_generic(method->return_type, method_bindings, method_binding_len);
          set_expr_resolved_type(expr, return_type);
          if (!apply_checked_call_storage_effects(program, expr, scope, diag)) {
            free(return_type);
            generic_bindings_free(method_bindings, method_binding_len);
            free(method_bindings);
            return false;
          }
          free(return_type);
          generic_bindings_free(method_bindings, method_binding_len);
          free(method_bindings);
          return true;
        }
        const Expr *receiver = expr->left->left;
        ZBuf callee_name;
        zbuf_init(&callee_name);
        member_name_buf(expr->left, &callee_name);
        bool builtin_member_callee = strncmp(callee_name.data, "std.", strlen("std.")) == 0 ||
          std_call_arg_count(callee_name.data) >= 0 ||
          is_world_stream_write_callee(expr->left, scope);
        zbuf_free(&callee_name);
        bool namespace_owner = receiver && receiver->kind == EXPR_IDENT && find_shape(program, receiver->text);
        char **receiver_constraint_args = NULL;
        size_t receiver_constraint_arg_len = 0;
        bool constrained_owner = receiver && receiver->kind == EXPR_IDENT &&
          constrained_interface_for_type_param(program, checking_function, receiver->text, &receiver_constraint_args, &receiver_constraint_arg_len);
        free_type_arg_list(receiver_constraint_args, receiver_constraint_arg_len);
        if (receiver && !namespace_owner && !constrained_owner && !builtin_member_callee) {
          if (!check_expr(program, receiver, scope, diag)) return false;
          const char *receiver_type_raw = expr_type(program, receiver, scope);
          char receiver_type_buf[192];
          snprintf(receiver_type_buf, sizeof(receiver_type_buf), "%s", receiver_type_raw ? receiver_type_raw : "Unknown");
          const char *receiver_type = receiver_type_buf;
          char owner_type[192];
          strip_ref_like_type(receiver_type, owner_type, sizeof(owner_type));
          const Shape *receiver_shape = find_shape_for_type(program, owner_type);
          if (receiver_shape) {
            const Function *receiver_method = find_shape_method_decl(receiver_shape, expr->left->text);
            if (!receiver_method) {
              char message[256];
              snprintf(message, sizeof(message), "shape '%s' has no receiver method '%s'", receiver_shape->name, expr->left->text);
              return set_diag_detail(diag, 3048, message, expr->line, expr->column, "method declared on receiver shape", expr->left->text, "rename the method or call a declared shape method");
            }
            bool receiver_requires_mut = false;
            if (!shape_method_receiver_info(receiver_method, &receiver_requires_mut)) {
              char message[256];
              snprintf(message, sizeof(message), "method '%s.%s' is not a receiver method", receiver_shape->name, receiver_method->name);
              return set_diag_detail(diag, 3048, message, expr->line, expr->column, "method whose first parameter is self: ref<Self> or self: mutref<Self>", "static method without self receiver", "call this method through the shape namespace or add an explicit self parameter");
            }
            if (receiver_method->params.len != expr->args.len + 1) {
              char message[256];
              snprintf(message, sizeof(message), "receiver method '%s.%s' expects %zu argument(s), got %zu", receiver_shape->name, receiver_method->name, receiver_method->params.len - 1, expr->args.len);
              return set_diag_detail(diag, 3004, message, expr->line, expr->column, "matching receiver method argument count", "wrong argument count", "update the receiver method call or signature");
            }
            char receiver_ref_inner[192];
            bool receiver_is_ref = named_ref_inner_text(receiver_type, "ref", receiver_ref_inner, sizeof(receiver_ref_inner));
            bool receiver_is_mutref = named_ref_inner_text(receiver_type, "mutref", receiver_ref_inner, sizeof(receiver_ref_inner));
            if (receiver_requires_mut) {
              if (receiver_is_ref) {
                return set_diag_detail(diag, 3049, "receiver method requires a mutable receiver", receiver->line, receiver->column, "mutref<Self> receiver or mutable shape lvalue", receiver_type, "call the method on a let mut value or pass a mutref receiver");
              }
              if (!receiver_is_mutref) {
                if (!expr_is_addressable(receiver)) {
                  return set_diag_detail(diag, 3049, "receiver method requires an addressable mutable receiver", receiver->line, receiver->column, "let mut shape value", "temporary receiver", "store the value in a let mut binding before calling the mutating method");
                }
                char root[128];
                if (expr_root_ident(receiver, root, sizeof(root)) && !scope_is_mutable(scope, root)) {
                  return set_diag_detail(diag, 3049, "receiver method requires a mutable receiver", receiver->line, receiver->column, "let mut receiver binding", "immutable receiver binding", "declare the receiver with let mut before calling a mutating method");
                }
                char lvalue_type[192];
                if (!check_lvalue_target(program, receiver, scope, diag, lvalue_type, sizeof(lvalue_type))) return false;
              }
            } else if (!receiver_is_ref && !receiver_is_mutref && !expr_is_addressable(receiver)) {
              return set_diag_detail(diag, 3049, "receiver method requires an addressable receiver", receiver->line, receiver->column, "shape lvalue or ref<Self>", "temporary receiver", "store the value in a binding before calling the method");
            }
            char root[128];
            char path[256];
            if (expr_binding_path(receiver, root, sizeof(root), path, sizeof(path)) && !check_borrow_conflict_at(scope, root, path, receiver_requires_mut, diag, receiver)) return false;
            char *self_arg_type = receiver_self_arg_type(receiver_type, receiver_requires_mut);
            GenericBinding *receiver_bindings = NULL;
            size_t receiver_binding_len = 0;
            if (!build_receiver_shape_method_bindings(program, receiver_shape, receiver_method, expr, self_arg_type, scope, diag, &receiver_bindings, &receiver_binding_len)) {
              free(self_arg_type);
              return false;
            }
            char *expected_self = type_substitute_generic(receiver_method->params.items[0].type, receiver_bindings, receiver_binding_len);
            if (!types_compatible(expected_self, self_arg_type)) {
              bool ok = set_diag_detail(diag, 3047, "receiver Self type does not match method receiver", receiver->line, receiver->column, expected_self, self_arg_type, "call the method on a receiver with the expected shape instantiation");
              free(expected_self);
              free(self_arg_type);
              generic_bindings_free(receiver_bindings, receiver_binding_len);
              free(receiver_bindings);
              return ok;
            }
            free(expected_self);
            free(self_arg_type);
            for (size_t i = 0; i < expr->args.len; i++) {
              char *expected_type = type_substitute_generic(receiver_method->params.items[i + 1].type, receiver_bindings, receiver_binding_len);
              if (!check_expr_expected(program, expr->args.items[i], scope, diag, expected_type)) {
                free(expected_type);
                generic_bindings_free(receiver_bindings, receiver_binding_len);
                free(receiver_bindings);
                return false;
              }
              const char *actual = expr_type(program, expr->args.items[i], scope);
              if (!types_compatible(expected_type, actual)) {
                char message[256];
                snprintf(message, sizeof(message), "argument %zu to receiver method '%s.%s' has incompatible type", i + 1, receiver_shape->name, receiver_method->name);
                bool ok = type_static_value_mismatch(program, expected_type, actual)
                  ? set_diag_detail(diag, 3047, "receiver method static value does not match Self instantiation", expr->args.items[i]->line, expr->args.items[i]->column, expected_type, actual, "call the method with one matching generic shape instantiation")
                  : set_diag_detail(diag, 3005, message, expr->args.items[i]->line, expr->args.items[i]->column, expected_type, actual, "pass a value matching the receiver method parameter");
                free(expected_type);
                generic_bindings_free(receiver_bindings, receiver_binding_len);
                free(receiver_bindings);
                return ok;
              }
              mark_owned_move_if_needed(expr->args.items[i], scope, expected_type);
              free(expected_type);
            }
            if (function_has_error_flow(program, receiver_method)) {
              if (allow_fallible_call == 0) {
                generic_bindings_free(receiver_bindings, receiver_binding_len);
                free(receiver_bindings);
                return set_diag_detail(diag, 1003, "fallible receiver method call must be checked", expr->line, expr->column, "check receiver.method(...)", "unchecked fallible receiver method", "prefix the call with check in a function marked raises");
              }
              if (!function_error_sets_compatible(checking_function, receiver_method, diag, expr)) {
                generic_bindings_free(receiver_bindings, receiver_binding_len);
                free(receiver_bindings);
                return false;
              }
            }
            char *return_type = type_substitute_generic(receiver_method->return_type, receiver_bindings, receiver_binding_len);
            set_expr_resolved_type(expr, return_type);
            if (!apply_checked_call_storage_effects(program, expr, scope, diag)) {
              free(return_type);
              generic_bindings_free(receiver_bindings, receiver_binding_len);
              free(receiver_bindings);
              return false;
            }
            free(return_type);
            generic_bindings_free(receiver_bindings, receiver_binding_len);
            free(receiver_bindings);
            return true;
          }
        }
        const InterfaceDecl *interface = NULL;
        const Function *required = find_constrained_interface_method(program, expr->left, &interface);
        if (required && interface) {
          if (required->params.len != expr->args.len) {
            char message[256];
            snprintf(message, sizeof(message), "interface method '%s.%s' expects %zu argument(s), got %zu", interface->name, required->name, required->params.len, expr->args.len);
            return set_diag_detail(diag, 3040, message, expr->line, expr->column, "matching interface method argument count", "wrong argument count", "update the constrained static call or interface signature");
          }
          char **constraint_args = NULL;
          size_t constraint_arg_len = 0;
          constrained_interface_for_type_param(program, checking_function, expr->left->left->text, &constraint_args, &constraint_arg_len);
          GenericBinding *interface_bindings = z_checked_calloc(interface->type_params.len, sizeof(GenericBinding));
          for (size_t i = 0; i < interface->type_params.len; i++) {
            interface_bindings[i].name = interface->type_params.items[i].name;
            interface_bindings[i].type = z_strdup(i < constraint_arg_len ? constraint_args[i] : "Unknown");
          }
          for (size_t i = 0; i < expr->args.len; i++) {
            char *expected_type = type_substitute_generic(required->params.items[i].type, interface_bindings, interface->type_params.len);
            if (!check_expr_expected(program, expr->args.items[i], scope, diag, expected_type)) {
              free(expected_type);
              generic_bindings_free(interface_bindings, interface->type_params.len);
              free(interface_bindings);
              free_type_arg_list(constraint_args, constraint_arg_len);
              return false;
            }
            const char *actual = expr_type(program, expr->args.items[i], scope);
            if (!types_compatible(expected_type, actual)) {
              char message[256];
              snprintf(message, sizeof(message), "argument %zu to constrained method '%s.%s' has incompatible type", i + 1, interface->name, required->name);
              bool ok = set_diag_detail(diag, 3042, message, expr->args.items[i]->line, expr->args.items[i]->column, expected_type, actual, "pass a value matching the interface method parameter");
              free(expected_type);
              generic_bindings_free(interface_bindings, interface->type_params.len);
              free(interface_bindings);
              free_type_arg_list(constraint_args, constraint_arg_len);
              return ok;
            }
            mark_owned_move_if_needed(expr->args.items[i], scope, expected_type);
            free(expected_type);
          }
          if (function_has_error_flow(program, required)) {
            char actual[160];
            snprintf(actual, sizeof(actual), "call to '%s.%s'", interface->name, required->name);
            if (!check_fallible_call_is_checked(program, required, expr, diag, "fallible constrained interface method call must be checked", "check Interface.method(...)", actual, "prefix the call with check in a function marked raises")) {
              generic_bindings_free(interface_bindings, interface->type_params.len);
              free(interface_bindings);
              free_type_arg_list(constraint_args, constraint_arg_len);
              return false;
            }
          }
          char *return_type = type_substitute_generic(required->return_type, interface_bindings, interface->type_params.len);
          set_expr_resolved_type(expr, return_type);
          if (!apply_checked_call_storage_effects(program, expr, scope, diag)) {
            free(return_type);
            generic_bindings_free(interface_bindings, interface->type_params.len);
            free(interface_bindings);
            free_type_arg_list(constraint_args, constraint_arg_len);
            return false;
          }
          free(return_type);
          generic_bindings_free(interface_bindings, interface->type_params.len);
          free(interface_bindings);
          free_type_arg_list(constraint_args, constraint_arg_len);
          return true;
        }
      }
      if (!check_call_callee(program, expr->left, scope, diag)) return false;
      if (expr->left && expr->left->kind == EXPR_IDENT) {
        const Function *fun = find_function(program, expr->left->text);
        if (fun && fun->params.len != expr->args.len) {
          char message[256];
          snprintf(message, sizeof(message), "function '%s' expects %zu argument(s), got %zu", fun->name, fun->params.len, expr->args.len);
          return set_diag_detail(diag, 3004, message, expr->line, expr->column, "matching argument count", "wrong argument count", "update the call or function signature");
        }
        const TypeArgVec *type_args = call_type_args(expr);
        if (fun && !function_is_generic(fun) && type_args && type_args->len > 0) {
          return set_diag_detail(diag, 3032, "non-generic function cannot take type arguments", expr->line, expr->column, "call without <...>", "type arguments on non-generic function", "remove the explicit type arguments or make the function generic");
        }
        if (fun && function_is_generic(fun)) {
          GenericBinding *bindings = z_checked_calloc(fun->type_params.len, sizeof(GenericBinding));
          for (size_t binding_index = 0; binding_index < fun->type_params.len; binding_index++) bindings[binding_index].name = fun->type_params.items[binding_index].name;
          if (!build_generic_bindings(program, fun, expr, scope, diag, bindings, fun->type_params.len, expected)) {
            generic_bindings_free(bindings, fun->type_params.len);
            free(bindings);
            return false;
          }
          if (!validate_generic_constraints(program, fun, expr, diag, bindings, fun->type_params.len)) {
            generic_bindings_free(bindings, fun->type_params.len);
            free(bindings);
            return false;
          }
          for (size_t i = 0; i < expr->args.len; i++) {
            char *expected_type = i < fun->params.len ? type_substitute_generic(fun->params.items[i].type, bindings, fun->type_params.len) : z_strdup("Unknown");
            if (!check_expr_expected(program, expr->args.items[i], scope, diag, expected_type)) {
              free(expected_type);
              generic_bindings_free(bindings, fun->type_params.len);
              free(bindings);
              return false;
            }
            const char *actual = expr_type(program, expr->args.items[i], scope);
            if (!types_compatible(expected_type, actual)) {
              char message[256];
              snprintf(message, sizeof(message), "argument %zu to generic '%s' has incompatible type", i + 1, fun->name);
              bool ok = type_static_value_mismatch(program, expected_type, actual)
                ? set_diag_detail(diag, 3045, "static value argument does not match the annotated value", expr->args.items[i]->line, expr->args.items[i]->column, expected_type, actual, "pass the same static value or remove the conflicting explicit argument")
                : set_diag_detail(diag, 3005, message, expr->args.items[i]->line, expr->args.items[i]->column, expected_type, actual, "pass a value matching the resolved generic parameter");
              free(expected_type);
              generic_bindings_free(bindings, fun->type_params.len);
              free(bindings);
              return ok;
            }
            mark_owned_move_if_needed(expr->args.items[i], scope, expected_type);
            free(expected_type);
          }
          if (function_has_error_flow(program, fun)) {
            char actual[160];
            snprintf(actual, sizeof(actual), "call to '%s'", fun->name);
            if (!check_fallible_call_is_checked(program, fun, expr, diag, "fallible generic function call must be checked", "check fallible_call(...)", actual, "prefix the call with check in a function marked raises")) {
              generic_bindings_free(bindings, fun->type_params.len);
              free(bindings);
              return false;
            }
          }
          char *return_type = type_substitute_generic(fun->return_type, bindings, fun->type_params.len);
          set_expr_resolved_type(expr, return_type);
          if (!apply_checked_call_storage_effects(program, expr, scope, diag)) {
            free(return_type);
            generic_bindings_free(bindings, fun->type_params.len);
            free(bindings);
            return false;
          }
          free(return_type);
          generic_bindings_free(bindings, fun->type_params.len);
          free(bindings);
          return true;
        }
      }
      for (size_t i = 0; i < expr->args.len; i++) {
        if (expr->left && expr->left->kind == EXPR_IDENT) {
          const Function *fun = find_function(program, expr->left->text);
          if (fun && i < fun->params.len && !check_expr_expected(program, expr->args.items[i], scope, diag, fun->params.items[i].type)) return false;
          if (fun && i < fun->params.len && !types_compatible(fun->params.items[i].type, expr_type(program, expr->args.items[i], scope))) {
            char message[256];
            snprintf(message, sizeof(message), "argument %zu to '%s' has incompatible type", i + 1, fun->name);
            return set_diag_detail(diag, 3005, message, expr->args.items[i]->line, expr->args.items[i]->column, fun->params.items[i].type, expr_type(program, expr->args.items[i], scope), "pass a compatible value");
          }
          if (fun && i < fun->params.len) mark_owned_move_if_needed(expr->args.items[i], scope, fun->params.items[i].type);
        } else {
          if (!check_expr(program, expr->args.items[i], scope, diag)) return false;
        }
      }
      if (expr->left && expr->left->kind == EXPR_IDENT) {
        const Function *fun = find_function(program, expr->left->text);
        if (fun && !function_is_generic(fun) && !apply_checked_call_storage_effects(program, expr, scope, diag)) return false;
      }
      if (is_world_stream_write_call(expr, scope)) {
        if (expr->args.len != 1) return set_diag_detail(diag, 3004, "World stream write expects one argument", expr->line, expr->column, "world.out.write(text)", "wrong argument count", "pass exactly one String or byte span argument");
        if (!check_expr_expected(program, expr->args.items[0], scope, diag, "String")) return false;
        const char *actual = expr_type(program, expr->args.items[0], scope);
        if (!types_compatible("String", actual) && !types_compatible("Span<u8>", actual)) {
          return set_diag_detail(diag, 3005, "World stream write argument has incompatible type", expr->args.items[0]->line, expr->args.items[0]->column, "String or Span<u8>", actual, "pass text or a byte span to the stream writer");
        }
        if (allow_fallible_call == 0) {
          return set_diag_detail(diag, 1003, "fallible World stream write must be checked", expr->line, expr->column, "check world.out.write(...)", "unchecked World stream write", "prefix the write with check in a function marked raises");
        }
        set_expr_resolved_type(expr, "Void");
        return true;
      }
      {
        const Function *fun = fallible_callee(program, expr);
        if ((fun || is_builtin_fallible_call(expr)) && allow_fallible_call == 0) {
          char actual[160];
          snprintf(actual, sizeof(actual), "call to '%s'", fun ? fun->name : "std fs fallible helper");
          return set_diag_detail(diag, 1003, "fallible function call must be checked", expr->line, expr->column, "check fallible_call(...)", actual, "prefix the call with check in a function marked raises");
        }
      }
      if (expr->left && expr->left->kind == EXPR_MEMBER) {
        ZBuf std_name;
        zbuf_init(&std_name);
        member_name_buf(expr->left, &std_name);
        int expected_count = std_call_arg_count(std_name.data);
        if (expected_count >= 0) {
          if ((size_t)expected_count != expr->args.len) {
            char message[256];
            snprintf(message, sizeof(message), "std function '%s' expects %d argument(s), got %zu", std_name.data, expected_count, expr->args.len);
            zbuf_free(&std_name);
            return set_diag_detail(diag, 3011, message, expr->line, expr->column, "matching std helper signature", "wrong argument count", "update the std helper call");
          }
          if (strcmp(std_name.data, "std.mem.len") == 0) {
            if (!check_expr(program, expr->args.items[0], scope, diag)) {
              zbuf_free(&std_name);
              return false;
            }
            const char *actual = expr_type(program, expr->args.items[0], scope);
            char element_type[128];
            if (strcmp(actual, "String") != 0 &&
                !span_element_text(actual, element_type, sizeof(element_type)) &&
                !fixed_array_type_parts(actual, NULL, 0, element_type, sizeof(element_type))) {
              zbuf_free(&std_name);
              return set_diag_detail(diag, 3012, "std.mem.len expects a String, Span<T>, or fixed array", expr->args.items[0]->line, expr->args.items[0]->column, "String, Span<T>, or [N]T", actual, "pass a byte-oriented string, span, or fixed array value");
            }
            set_expr_resolved_type(expr, "usize");
            zbuf_free(&std_name);
            return true;
          }
          if (strcmp(std_name.data, "std.mem.get") == 0) {
            if (!check_expr(program, expr->args.items[0], scope, diag)) {
              zbuf_free(&std_name);
              return false;
            }
            const char *actual = expr_type(program, expr->args.items[0], scope);
            char element_type[128];
            if (!index_element_type(actual, element_type, sizeof(element_type))) {
              zbuf_free(&std_name);
              return set_diag_detail(diag, 3012, "std.mem.get expects an indexable value", expr->args.items[0]->line, expr->args.items[0]->column, "[N]T, Span<T>, MutSpan<T>, or String", actual, "pass an indexable value and handle the Maybe<T> result");
            }
            if (!check_expr_expected(program, expr->args.items[1], scope, diag, "usize")) {
              zbuf_free(&std_name);
              return false;
            }
            const char *index_type = expr_type(program, expr->args.items[1], scope);
            if (!is_int_type(index_type)) {
              zbuf_free(&std_name);
              return set_diag_detail(diag, 3028, "std.mem.get index must be an integer", expr->args.items[1]->line, expr->args.items[1]->column, "integer index", index_type, "use an integer expression such as usize or a checked integer literal");
            }
            char result_type[160];
            snprintf(result_type, sizeof(result_type), "Maybe<%s>", element_type);
            set_expr_resolved_type(expr, result_type);
            zbuf_free(&std_name);
            return true;
          }
          if (strcmp(std_name.data, "std.mem.eqlBytes") == 0) {
            if (!check_expr(program, expr->args.items[0], scope, diag) || !check_expr(program, expr->args.items[1], scope, diag)) {
              zbuf_free(&std_name);
              return false;
            }
            const char *left_type = expr_type(program, expr->args.items[0], scope);
            const char *right_type = expr_type(program, expr->args.items[1], scope);
            char left_element[128];
            char right_element[128];
            if (!span_element_text(left_type, left_element, sizeof(left_element)) || !span_element_text(right_type, right_element, sizeof(right_element))) {
              zbuf_free(&std_name);
              return set_diag_detail(diag, 3012, "std.mem.eqlBytes expects Span<T> arguments", expr->line, expr->column, "two Span<T> values", "non-span argument", "pass spans with matching element types");
            }
            if (!types_compatible(left_element, right_element)) {
              zbuf_free(&std_name);
              return set_diag_detail(diag, 3012, "std.mem.eqlBytes span element types must match", expr->line, expr->column, left_element, right_element, "compare spans with the same element type");
            }
            set_expr_resolved_type(expr, "Bool");
            zbuf_free(&std_name);
            return true;
          }
          if (strcmp(std_name.data, "std.mem.allocBytes") == 0) {
            if (!check_expr(program, expr->args.items[0], scope, diag)) {
              zbuf_free(&std_name);
              return false;
            }
            const char *alloc_type = expr_type(program, expr->args.items[0], scope);
            if (!is_allocator_type(alloc_type)) {
              zbuf_free(&std_name);
              return set_diag_detail(diag, 3012, "std.mem.allocBytes expects an allocator primitive", expr->args.items[0]->line, expr->args.items[0]->column, "NullAlloc or mutable FixedBufAlloc", alloc_type, "use std.mem.nullAlloc() or a let mut FixedBufAlloc from std.mem.fixedBufAlloc(buffer)");
            }
            if (strcmp(alloc_type, "FixedBufAlloc") == 0 &&
                (expr->args.items[0]->kind != EXPR_IDENT || !scope_is_mutable(scope, expr->args.items[0]->text))) {
              zbuf_free(&std_name);
              return set_diag_detail(diag, 3012, "std.mem.allocBytes requires a mutable FixedBufAlloc binding", expr->args.items[0]->line, expr->args.items[0]->column, "let mut allocator: FixedBufAlloc", "immutable or temporary FixedBufAlloc", "store the fixed buffer allocator in a let mut binding before allocating");
            }
            if (!check_expr_expected(program, expr->args.items[1], scope, diag, "usize")) {
              zbuf_free(&std_name);
              return false;
            }
            const char *len_type = expr_type(program, expr->args.items[1], scope);
            if (!is_int_type(len_type)) {
              zbuf_free(&std_name);
              return set_diag_detail(diag, 3028, "allocation length must be an integer", expr->args.items[1]->line, expr->args.items[1]->column, "usize length", len_type, "pass an integer byte count");
            }
            set_expr_resolved_type(expr, "Maybe<MutSpan<u8>>");
            zbuf_free(&std_name);
            return true;
          }
          if (strcmp(std_name.data, "std.mem.byteBuf") == 0) {
            if (!check_expr(program, expr->args.items[0], scope, diag)) {
              zbuf_free(&std_name);
              return false;
            }
            const char *alloc_type = expr_type(program, expr->args.items[0], scope);
            if (!is_allocator_type(alloc_type)) {
              zbuf_free(&std_name);
              return set_diag_detail(diag, 3012, "std.mem.byteBuf expects an allocator primitive", expr->args.items[0]->line, expr->args.items[0]->column, "NullAlloc or mutable FixedBufAlloc", alloc_type, "use std.mem.nullAlloc() or a let mut FixedBufAlloc from std.mem.fixedBufAlloc(buffer)");
            }
            if (strcmp(alloc_type, "FixedBufAlloc") == 0 &&
                (expr->args.items[0]->kind != EXPR_IDENT || !scope_is_mutable(scope, expr->args.items[0]->text))) {
              zbuf_free(&std_name);
              return set_diag_detail(diag, 3012, "std.mem.byteBuf requires a mutable FixedBufAlloc binding", expr->args.items[0]->line, expr->args.items[0]->column, "let mut allocator: FixedBufAlloc", "immutable or temporary FixedBufAlloc", "store the fixed buffer allocator in a let mut binding before allocating a ByteBuf");
            }
            if (!check_expr_expected(program, expr->args.items[1], scope, diag, "usize")) {
              zbuf_free(&std_name);
              return false;
            }
            const char *len_type = expr_type(program, expr->args.items[1], scope);
            if (!is_int_type(len_type)) {
              zbuf_free(&std_name);
              return set_diag_detail(diag, 3028, "ByteBuf length must be an integer", expr->args.items[1]->line, expr->args.items[1]->column, "usize length", len_type, "pass an integer byte count");
            }
            set_expr_resolved_type(expr, "Maybe<owned<ByteBuf>>");
            zbuf_free(&std_name);
            return true;
          }
          if (strcmp(std_name.data, "std.fs.read") == 0) {
            if (!check_expr(program, expr->args.items[0], scope, diag)) {
              zbuf_free(&std_name);
              return false;
            }
            const char *file_or_path_type = expr_type(program, expr->args.items[0], scope);
            const char *first_expected = "String";
            const char *return_type = "usize";
            if (type_is_named_generic(file_or_path_type, "ref") || type_is_named_generic(file_or_path_type, "mutref")) {
              first_expected = "mutref<File>";
              return_type = "Maybe<usize>";
            }
            if (!types_compatible(first_expected, file_or_path_type)) {
              zbuf_free(&std_name);
              return set_diag_detail(diag, 3012, "std.fs.read expects a path or mutable File reference", expr->args.items[0]->line, expr->args.items[0]->column, "String path or mutref<File>", file_or_path_type, "use std.fs.readBytes(path, buffer) for paths or std.fs.read(&mut file, buffer) for file resources");
            }
            if (!check_expr_expected(program, expr->args.items[1], scope, diag, "MutSpan<u8>")) {
              zbuf_free(&std_name);
              return false;
            }
            const char *buf_type = expr_type(program, expr->args.items[1], scope);
            if (!types_compatible("MutSpan<u8>", buf_type)) {
              zbuf_free(&std_name);
              return set_diag_detail(diag, 3012, "std.fs.read expects a mutable byte buffer", expr->args.items[1]->line, expr->args.items[1]->column, "MutSpan<u8>", buf_type, "pass a mutable byte array or MutSpan<u8>");
            }
            set_expr_resolved_type(expr, return_type);
            zbuf_free(&std_name);
            return true;
          }
          if (strcmp(std_name.data, "std.fs.readAll") == 0 || strcmp(std_name.data, "std.fs.readAllOrRaise") == 0) {
            if (!check_expr(program, expr->args.items[0], scope, diag)) {
              zbuf_free(&std_name);
              return false;
            }
            const char *alloc_type = expr_type(program, expr->args.items[0], scope);
            if (!is_allocator_type(alloc_type)) {
              zbuf_free(&std_name);
              return set_diag_detail(diag, 3012, "std.fs.readAll expects an allocator primitive", expr->args.items[0]->line, expr->args.items[0]->column, "NullAlloc or mutable FixedBufAlloc", alloc_type, "pass an explicit allocator; no global filesystem allocator is available");
            }
            if (strcmp(alloc_type, "FixedBufAlloc") == 0 &&
                (expr->args.items[0]->kind != EXPR_IDENT || !scope_is_mutable(scope, expr->args.items[0]->text))) {
              zbuf_free(&std_name);
              return set_diag_detail(diag, 3012, "std.fs.readAll requires a mutable FixedBufAlloc binding", expr->args.items[0]->line, expr->args.items[0]->column, "let mut allocator: FixedBufAlloc", "immutable or temporary FixedBufAlloc", "store the fixed buffer allocator in a let mut binding before reading");
            }
            for (size_t i = 1; i < expr->args.len; i++) {
              const char *expected = std_call_arg_type(std_name.data, i);
              if (!check_expr_expected(program, expr->args.items[i], scope, diag, expected)) {
                zbuf_free(&std_name);
                return false;
              }
              const char *actual = expr_type(program, expr->args.items[i], scope);
              if (expected && !types_compatible(expected, actual)) {
                char message[256];
                snprintf(message, sizeof(message), "argument %zu to '%s' has incompatible type", i + 1, std_name.data);
                zbuf_free(&std_name);
                return set_diag_detail(diag, 3012, message, expr->args.items[i]->line, expr->args.items[i]->column, expected, actual, "pass a compatible value");
              }
            }
            set_expr_resolved_type(expr, strcmp(std_name.data, "std.fs.readAllOrRaise") == 0 ? "owned<ByteBuf>" : "Maybe<owned<ByteBuf>>");
            zbuf_free(&std_name);
            return true;
          }
          if (strcmp(std_name.data, "std.json.parse") == 0 || strcmp(std_name.data, "std.json.parseBytes") == 0) {
            if (!check_expr(program, expr->args.items[0], scope, diag)) {
              zbuf_free(&std_name);
              return false;
            }
            const char *alloc_type = expr_type(program, expr->args.items[0], scope);
            if (!is_allocator_type(alloc_type)) {
              char message[256];
              snprintf(message, sizeof(message), "%s expects an allocator primitive", std_name.data);
              zbuf_free(&std_name);
              return set_diag_detail(diag, 3012, message, expr->args.items[0]->line, expr->args.items[0]->column, "NullAlloc or mutable FixedBufAlloc", alloc_type, "pass an explicit allocator; JSON parsing does not use a global allocator");
            }
            if (strcmp(alloc_type, "FixedBufAlloc") == 0 &&
                (expr->args.items[0]->kind != EXPR_IDENT || !scope_is_mutable(scope, expr->args.items[0]->text))) {
              char message[256];
              snprintf(message, sizeof(message), "%s requires a mutable FixedBufAlloc binding", std_name.data);
              zbuf_free(&std_name);
              return set_diag_detail(diag, 3012, message, expr->args.items[0]->line, expr->args.items[0]->column, "let mut allocator: FixedBufAlloc", "immutable or temporary FixedBufAlloc", "store the fixed buffer allocator in a let mut binding before parsing JSON");
            }
            const char *expected = strcmp(std_name.data, "std.json.parseBytes") == 0 ? "Span<u8>" : "String";
            if (!check_expr_expected(program, expr->args.items[1], scope, diag, expected)) {
              zbuf_free(&std_name);
              return false;
            }
            const char *actual = expr_type(program, expr->args.items[1], scope);
            if (!types_compatible(expected, actual)) {
              char message[256];
              snprintf(message, sizeof(message), "argument 2 to '%s' has incompatible type", std_name.data);
              zbuf_free(&std_name);
              return set_diag_detail(diag, 3012, message, expr->args.items[1]->line, expr->args.items[1]->column, expected, actual, "pass a compatible JSON payload");
            }
            set_expr_resolved_type(expr, "Maybe<JsonDoc>");
            zbuf_free(&std_name);
            return true;
          }
          for (size_t i = 0; i < expr->args.len; i++) {
            const char *expected = std_call_arg_type(std_name.data, i);
            if (!check_expr_expected(program, expr->args.items[i], scope, diag, expected)) {
              zbuf_free(&std_name);
              return false;
            }
            const char *actual = expr_type(program, expr->args.items[i], scope);
            if (expr->args.items[i]->kind == EXPR_IDENT) {
              const char *scope_actual = scope_type(scope, expr->args.items[i]->text);
              if (scope_actual) actual = scope_actual;
            }
            bool span_array_arg = false;
            bool expected_span = expected && type_is_named_generic(expected, "Span");
            bool expected_mut_span = expected && type_is_named_generic(expected, "MutSpan");
            bool inline_array_literal = expr->args.items[i] && expr->args.items[i]->kind == EXPR_ARRAY_LITERAL;
            if (expected && actual && actual[0] == '[' && (expected_span || (expected_mut_span && !inline_array_literal))) {
              char expected_element[128];
              char actual_element[128];
              if (span_element_text(expected, expected_element, sizeof(expected_element)) &&
                  fixed_array_type_parts(actual, NULL, 0, actual_element, sizeof(actual_element))) {
                span_array_arg = types_compatible(expected_element, actual_element);
              }
            }
            if (expected && !span_array_arg && !types_compatible(expected, actual)) {
              char message[256];
              snprintf(message, sizeof(message), "argument %zu to '%s' has incompatible type", i + 1, std_name.data);
              zbuf_free(&std_name);
              return set_diag_detail(diag, 3012, message, expr->args.items[i]->line, expr->args.items[i]->column, expected, actual, "pass a compatible value");
            }
          }
          set_expr_resolved_type(expr, std_call_return_type(expr->left));
        } else if (strncmp(std_name.data, "std.", strlen("std.")) == 0) {
          char message[256];
          snprintf(message, sizeof(message), "unknown std helper '%s'", std_name.data);
          zbuf_free(&std_name);
          return set_diag_detail(diag, 3011, message, expr->line, expr->column, "known std helper", "unknown std helper", "use a documented std helper name");
        }
        zbuf_free(&std_name);
      }
      if (!expr->resolved_type) set_expr_resolved_type(expr, expr_type(program, expr, scope));
      return true;
    case EXPR_CAST: {
      if (!validate_type_form(expr->text, diag, expr->line, expr->column)) return false;
      if (!check_expr(program, expr->left, scope, diag)) return false;
      const char *source_type = expr_type(program, expr->left, scope);
      if (!is_primitive_cast_type(source_type) || !is_primitive_cast_type(expr->text)) {
        char actual[128];
        snprintf(actual, sizeof(actual), "%s as %s", source_type, expr->text ? expr->text : "Unknown");
        return set_diag_detail(diag, 3023, "cast requires primitive numeric or byte char types", expr->line, expr->column, "integer, float, or char as integer, float, or char", actual, "cast only between primitive numeric types and byte-sized char");
      }
      set_expr_resolved_type(expr, expr->text);
      return true;
    }
    case EXPR_BINARY: {
      bool comparison = strcmp(expr->text, "==") == 0 || strcmp(expr->text, "!=") == 0 || strcmp(expr->text, "<") == 0 ||
        strcmp(expr->text, "<=") == 0 || strcmp(expr->text, ">") == 0 || strcmp(expr->text, ">=") == 0;
      bool logical = strcmp(expr->text, "&&") == 0 || strcmp(expr->text, "||") == 0;
      if (!check_expr_expected(program, expr->left, scope, diag, expected && (is_int_type(expected) || is_float_type(expected)) ? expected : NULL)) return false;
      const char *left_type = expr_type(program, expr->left, scope);
      if (logical) {
        ProvenanceScopeSnapshot *before_right = provenance_scope_snapshot_capture(scope);
        if (!check_expr_expected(program, expr->right, scope, diag, "Bool")) {
          provenance_scope_snapshot_restore(before_right);
          provenance_scope_snapshot_free(before_right);
          return false;
        }
        ProvenanceScopeSnapshot *right_after = provenance_scope_snapshot_capture(scope);
        bool left_is_bool_literal = expr->left && expr->left->kind == EXPR_BOOL;
        bool left_value = left_is_bool_literal && expr->left->bool_value;
        bool include_before = true;
        bool include_after = true;
        if (left_is_bool_literal && strcmp(expr->text, "&&") == 0) {
          include_before = !left_value;
          include_after = left_value;
        } else if (left_is_bool_literal && strcmp(expr->text, "||") == 0) {
          include_before = left_value;
          include_after = !left_value;
        }
        provenance_scope_snapshot_restore_optional_branch(before_right, right_after, include_before, include_after);
        provenance_scope_snapshot_free(right_after);
        provenance_scope_snapshot_free(before_right);
        const char *right_type = expr_type(program, expr->right, scope);
        if (!is_bool_type(left_type) || !is_bool_type(right_type)) return set_diag_detail(diag, 3006, "logical operators require Bool operands", expr->line, expr->column, "Bool operands", "non-Bool operand", "compare values explicitly before using && or ||");
        set_expr_resolved_type(expr, "Bool");
        return true;
      }
      const char *right_expected = (is_int_type(left_type) || is_float_type(left_type)) ? left_type : NULL;
      if (!check_expr_expected(program, expr->right, scope, diag, right_expected)) return false;
      const char *right_type = expr_type(program, expr->right, scope);
      if (comparison) {
        if (!types_compatible(left_type, right_type)) {
          return set_diag_detail(diag, 3006, "comparison operands must have matching types", expr->line, expr->column, left_type, right_type, "compare values with the same type");
        }
        bool equality = strcmp(expr->text, "==") == 0 || strcmp(expr->text, "!=") == 0;
        if (!equality && !((is_int_type(left_type) && is_int_type(right_type)) || (is_float_type(left_type) && is_float_type(right_type)))) {
          return set_diag_detail(diag, 3006, "ordered comparisons require numeric operands", expr->line, expr->column, "matching integer or float operands", left_type, "use == or != for non-numeric values");
        }
        set_expr_resolved_type(expr, "Bool");
        return true;
      }
      if (!((is_int_type(left_type) && is_int_type(right_type)) || (is_float_type(left_type) && is_float_type(right_type)))) {
        return set_diag_detail(diag, 3006, "arithmetic operators require matching numeric operands", expr->line, expr->column, "matching integer or float operands", is_char_type(left_type) || is_char_type(right_type) ? "char operand" : "mixed or non-numeric operand", "keep integers, floats, and chars separate");
      }
      if (strcmp(left_type, right_type) != 0) {
        return set_diag_detail(diag, 3006, "numeric operands must have the same type", expr->line, expr->column, left_type, right_type, "use matching numeric types until broader casts are supported");
      }
      set_expr_resolved_type(expr, left_type);
      return true;
    }
    case EXPR_SHAPE_LITERAL: {
      const Shape *shape = find_shape(program, expr->text);
      if (!shape) return set_diag_detail(diag, 3009, "unknown shape literal", expr->line, expr->column, "declared shape", expr->text, "declare the shape before constructing it");
      const char *expected_shape_type = expected;
      char expected_owned_inner[160];
      if (owned_inner_text(expected_shape_type, expected_owned_inner, sizeof(expected_owned_inner))) expected_shape_type = expected_owned_inner;
      if (expected_shape_type && strcmp(expected_shape_type, "Unknown") != 0) {
        const Shape *expected_shape = find_shape_for_type(program, type_strip_const(expected_shape_type));
        if (!expected_shape || strcmp(expected_shape->name, shape->name) != 0) {
          return set_diag_detail(diag, 3006, "shape literal type does not match expected type", expr->line, expr->column, shape->name, expected, "assign the shape literal to the matching shape type");
        }
      }
      GenericBinding *shape_bindings = NULL;
      size_t shape_binding_len = 0;
      if (shape->type_params.len > 0) {
        char **args = NULL;
        size_t arg_len = 0;
        if (!expected_shape_type || !type_generic_arg_list(resolve_alias_type(expected_shape_type), shape->name, &args, &arg_len) || arg_len != shape->type_params.len) {
          free_type_arg_list(args, arg_len);
          return set_diag_detail(diag, 3034, "generic shape literal requires an explicit annotated type", expr->line, expr->column, "let value: Shape<T> = Shape { ... }", expected ? expected : "untyped shape literal", "add an explicit generic shape type annotation");
        }
        shape_binding_len = shape->type_params.len;
        shape_bindings = z_checked_calloc(shape_binding_len, sizeof(GenericBinding));
        for (size_t i = 0; i < shape_binding_len; i++) {
          shape_bindings[i].name = shape->type_params.items[i].name;
          if (shape->type_params.items[i].is_static) {
            if (!is_static_value_param_type(program, shape->type_params.items[i].type)) {
              free_type_arg_list(args, arg_len);
              generic_bindings_free(shape_bindings, shape_binding_len);
              free(shape_bindings);
              return set_diag_detail(diag, 3043, "static value parameter type is not supported", shape->type_params.items[i].line, shape->type_params.items[i].column, "integer, Bool, or enum static parameter", shape->type_params.items[i].type, "use a concrete integer, Bool, or enum type for this static parameter");
            }
            shape_bindings[i].type = canonical_static_arg_for_type(program, args[i], shape->type_params.items[i].type);
            if (!shape_bindings[i].type) {
              const char *open_static_type = scope_type(scope, args[i]);
              if (open_static_type && types_compatible(shape->type_params.items[i].type, open_static_type) && is_static_value_param_type(program, open_static_type)) shape_bindings[i].type = z_strdup(args[i]);
            }
            if (!shape_bindings[i].type) {
              free_type_arg_list(args, arg_len);
              generic_bindings_free(shape_bindings, shape_binding_len);
              free(shape_bindings);
              return set_diag_detail(diag, 3044, "static value argument must be deterministic and concrete", expr->line, expr->column, static_value_expected_label(program, shape->type_params.items[i].type), args[i], "pass an explicit literal, top-level const, or supported meta value with the static parameter type");
            }
          } else {
            shape_bindings[i].type = z_strdup(args[i]);
          }
        }
        free_type_arg_list(args, arg_len);
      }
      for (size_t i = 0; i < expr->fields.len; i++) {
        FieldInit *field = &expr->fields.items[i];
        for (size_t previous = 0; previous < i; previous++) {
          if (strcmp(expr->fields.items[previous].name, field->name) == 0) {
            bool ok = set_diag_detail(diag, 3008, "duplicate shape literal field", field->line, field->column, "one initializer per field", field->name, "remove the duplicate field initializer");
            generic_bindings_free(shape_bindings, shape_binding_len);
            free(shape_bindings);
            return ok;
          }
        }
        const Param *shape_field = find_shape_field(shape, field->name);
        if (!shape_field) {
          char message[256];
          snprintf(message, sizeof(message), "shape '%s' has no field '%s'", shape->name, field->name);
          return set_diag_detail(diag, 3101, message, field->line, field->column, "declared shape field", field->name, "rename the field or update the shape");
        }
        char *field_type = shape_bindings ? type_substitute_generic(shape_field->type, shape_bindings, shape_binding_len) : z_strdup(shape_field->type);
        if (!check_expr_expected(program, field->value, scope, diag, field_type)) {
          free(field_type);
          generic_bindings_free(shape_bindings, shape_binding_len);
          free(shape_bindings);
          return false;
        }
        const char *actual = expr_type(program, field->value, scope);
        if (!types_compatible(field_type, actual)) {
          char message[256];
          snprintf(message, sizeof(message), "field '%s' has incompatible type", field->name);
          bool ok = set_diag_detail(diag, 3006, message, field->line, field->column, field_type, actual, "initialize the field with a compatible value");
          free(field_type);
          generic_bindings_free(shape_bindings, shape_binding_len);
          free(shape_bindings);
          return ok;
        }
        free(field_type);
      }
      for (size_t i = 0; i < shape->fields.len; i++) {
        bool seen = false;
        for (size_t j = 0; j < expr->fields.len; j++) {
          if (strcmp(shape->fields.items[i].name, expr->fields.items[j].name) == 0) seen = true;
        }
        if (!seen) {
          Param *shape_field = &shape->fields.items[i];
          if (!shape_field->default_value) {
            return set_diag_detail(diag, 3102, "shape literal is missing a field", expr->line, expr->column, shape_field->name, "field not initialized", "initialize the field or add a field default");
          }
          char *field_type = shape_bindings ? type_substitute_generic(shape_field->type, shape_bindings, shape_binding_len) : z_strdup(shape_field->type);
          if (!check_expr_expected(program, shape_field->default_value, scope, diag, field_type)) {
            free(field_type);
            generic_bindings_free(shape_bindings, shape_binding_len);
            free(shape_bindings);
            return false;
          }
          const char *actual = expr_type(program, shape_field->default_value, scope);
          if (!types_compatible(field_type, actual)) {
            char message[256];
            snprintf(message, sizeof(message), "default for field '%s' has incompatible type", shape_field->name);
            bool ok = set_diag_detail(diag, 3006, message, shape_field->default_value->line, shape_field->default_value->column, field_type, actual, "initialize the field default with a compatible value");
            free(field_type);
            generic_bindings_free(shape_bindings, shape_binding_len);
            free(shape_bindings);
            return ok;
          }
          free(field_type);
        }
      }
      char *normalized_shape_type = NULL;
      if (shape_bindings) {
        ZBuf type_buf;
        zbuf_init(&type_buf);
        zbuf_append(&type_buf, shape->name);
        zbuf_append_char(&type_buf, '<');
        for (size_t i = 0; i < shape_binding_len; i++) {
          if (i > 0) zbuf_append_char(&type_buf, ',');
          zbuf_append(&type_buf, shape_bindings[i].type ? shape_bindings[i].type : "Unknown");
        }
        zbuf_append_char(&type_buf, '>');
        normalized_shape_type = type_buf.data;
      }
      const char *literal_resolved_type = normalized_shape_type ? normalized_shape_type : shape->name;
      char owned_literal_inner[160];
      if (owned_inner_text(literal_resolved_type, owned_literal_inner, sizeof(owned_literal_inner))) literal_resolved_type = owned_literal_inner;
      set_expr_resolved_type(expr, literal_resolved_type);
      free(normalized_shape_type);
      generic_bindings_free(shape_bindings, shape_binding_len);
      free(shape_bindings);
      return true;
    }
    case EXPR_ARRAY_LITERAL: {
      const char *element_expected = NULL;
      char expected_len_text[64] = {0};
      char actual_len_text[64] = {0};
      char inferred_type[160];
      char inferred_element_type[160] = {0};
      if (expr->array_repeat) {
        if (expr->args.len != 2) {
          return set_diag_detail(diag, 3006, "array repeat literal requires a value and count", expr->line, expr->column, "[value; count]", "array literal", "write a repeated fixed-array literal such as [0_u8; 16]");
        }
        const Expr *count = expr->args.items[1];
        if (!count || (count->kind != EXPR_NUMBER && count->kind != EXPR_IDENT)) {
          return set_diag_detail(diag, 3006, "array repeat count must be a compile-time integer", count ? count->line : expr->line, count ? count->column : expr->column, "integer literal or static const", count ? "non-integer expression" : "missing count", "use a literal count such as [0_u8; 16]");
        }
        char *actual_static = canonical_static_arg(program, count->text);
        if (!actual_static) {
          return set_diag_detail(diag, 3006, "array repeat count must be a compile-time integer", count->line, count->column, "integer literal or static const", count->text ? count->text : "<unknown>", "use a literal count or a top-level integer const");
        }
        snprintf(actual_len_text, sizeof(actual_len_text), "%s", actual_static);
        free(actual_static);
      } else {
        snprintf(actual_len_text, sizeof(actual_len_text), "%zu", expr->args.len);
      }
      if (expected && expected[0] == '[') {
        const char *close = strchr(expected, ']');
        if (close && close[1]) {
          snprintf(expected_len_text, sizeof(expected_len_text), "%.*s", (int)(close - expected - 1), expected + 1);
          char *expected_static = canonical_static_arg(program, expected_len_text);
          bool length_ok = false;
          if (expected_static) length_ok = strcmp(expected_static, actual_len_text) == 0;
          else length_ok = strcmp(expected_len_text, actual_len_text) == 0;
          if (!length_ok) {
            char actual_detail[128];
            if (expr->array_repeat) snprintf(actual_detail, sizeof(actual_detail), "repeat count %s", actual_len_text);
            else snprintf(actual_detail, sizeof(actual_detail), "%zu element(s)", expr->args.len);
            bool ok = set_diag_detail(diag, 3006, "array literal length does not match expected fixed array", expr->line, expr->column, expected, actual_detail, "add or remove elements so the array literal length matches its fixed-array type");
            free(expected_static);
            return ok;
          }
          free(expected_static);
          element_expected = close + 1;
        }
      }
      size_t element_count = expr->array_repeat ? 1 : expr->args.len;
      for (size_t i = 0; i < element_count; i++) {
        if (!check_expr_expected(program, expr->args.items[i], scope, diag, element_expected)) return false;
        if (element_expected) {
          const char *actual = expr_type(program, expr->args.items[i], scope);
          if (!types_compatible(element_expected, actual)) {
            char message[256];
            snprintf(message, sizeof(message), "array literal element %zu has incompatible type", i + 1);
            return set_diag_detail(diag, 3006, message, expr->args.items[i]->line, expr->args.items[i]->column, element_expected, actual, "use elements whose types match the fixed-array element type");
          }
        } else {
          const char *actual = expr_type(program, expr->args.items[i], scope);
          if (i == 0) {
            snprintf(inferred_element_type, sizeof(inferred_element_type), "%s", actual ? actual : "Unknown");
          } else if (!types_compatible(inferred_element_type, actual)) {
            char message[256];
            snprintf(message, sizeof(message), "array literal element %zu has incompatible type", i + 1);
            return set_diag_detail(diag, 3006, message, expr->args.items[i]->line, expr->args.items[i]->column, inferred_element_type, actual, "annotate the array or use elements with one inferred element type");
          }
        }
      }
      if (expected && expected[0] == '[') {
        set_expr_resolved_type(expr, expected);
      } else if (expr->array_repeat && expr->args.len == 2) {
        snprintf(inferred_type, sizeof(inferred_type), "[%s]%s", actual_len_text, expr_type(program, expr->args.items[0], scope));
        set_expr_resolved_type(expr, inferred_type);
      } else if (expr->args.len > 0) {
        snprintf(inferred_type, sizeof(inferred_type), "[%zu]%s", expr->args.len, expr_type(program, expr->args.items[0], scope));
        set_expr_resolved_type(expr, inferred_type);
      } else {
        set_expr_resolved_type(expr, "[0]Unknown");
      }
      return true;
    }
    case EXPR_STRING:
    case EXPR_CHAR:
    case EXPR_NUMBER:
    case EXPR_BOOL:
    case EXPR_NULL:
      return true;
  }
  return true;
}

static bool check_expr(const Program *program, const Expr *expr, Scope *scope, ZDiag *diag) {
  return check_expr_expected(program, expr, scope, diag, NULL);
}

static void mark_owned_move_if_needed(const Expr *expr, Scope *scope, const char *destination_type) {
  if (!expr || expr->kind != EXPR_IDENT || !destination_type || !type_is_owned(destination_type)) return;
  const char *source_type = scope_type(scope, expr->text);
  if (source_type && type_is_owned(source_type)) {
    scope_set_moved(scope, expr->text, true);
    ((Expr *)expr)->moves_ownership = true;
  }
}

static void mark_owned_target_live_if_needed(const Expr *target, Scope *scope, const char *target_type) {
  if (!target || target->kind != EXPR_IDENT || !target_type || !type_is_owned(target_type)) return;
  scope_set_moved(scope, target->text, false);
}

static bool check_lvalue_target(const Program *program, const Expr *target, Scope *scope, ZDiag *diag, char *out_type, size_t out_type_len) {
  if (!target || !out_type || out_type_len == 0) {
    return set_diag_detail(diag, 3027, "unsupported assignment target", target ? target->line : 0, target ? target->column : 0, "mutable identifier, shape field, or fixed-array index", "unsupported assignment target", "assign through a mutable local lvalue");
  }
  switch (target->kind) {
    case EXPR_IDENT: {
      if (!scope_has(scope, target->text)) {
        char message[256];
        snprintf(message, sizeof(message), "unknown identifier '%s'", target->text ? target->text : "<assignment target>");
        return set_diag_detail(diag, 3003, message, target->line, target->column, "visible mutable local", "no matching visible symbol", "declare the name before assigning it");
      }
      if (!scope_is_mutable(scope, target->text)) {
        return set_diag_detail(diag, 3010, "cannot assign through immutable binding", target->line, target->column, "binding declared with let mut", "immutable binding", "add mut to the root binding before assigning through it");
      }
      const char *type = scope_type(scope, target->text);
      if (type_is_const(type)) {
        return set_diag_detail(diag, 3010, "cannot assign to const binding", target->line, target->column, "mutable non-const binding", type, "remove const from the binding type or write to a separate mutable value");
      }
      snprintf(out_type, out_type_len, "%s", type ? type : "Unknown");
      set_expr_resolved_type(target, out_type);
      return true;
    }
    case EXPR_MEMBER: {
      char root[128];
      char path[256];
      bool local_place = expr_binding_path(target->left, root, sizeof(root), path, sizeof(path)) && scope_has(scope, root);
      if (local_place) {
        if (!check_place_root_available(target->left, scope, root, diag)) return false;
        if (!check_place_index_exprs(program, target->left, scope, diag)) return false;
      } else if (!check_expr(program, target->left, scope, diag)) return false;
      const char *read_base_type = expr_type(program, target->left, scope);
      char base_type[128];
      snprintf(base_type, sizeof(base_type), "%s", read_base_type ? read_base_type : "Unknown");
      const char *shape_type = base_type;
      char ref_shape_type[128];
      if (named_ref_inner_text(shape_type, "ref", ref_shape_type, sizeof(ref_shape_type))) {
        return set_diag_detail(diag, 3010, "cannot assign through ref<T>", target->line, target->column, "mutref<T> or mutable shape lvalue", "ref<T>", "borrow with &mut when mutation is required");
      }
      if (named_ref_inner_text(shape_type, "mutref", ref_shape_type, sizeof(ref_shape_type))) {
        shape_type = ref_shape_type;
      } else if (!check_lvalue_target(program, target->left, scope, diag, base_type, sizeof(base_type))) {
        return false;
      }
      const Shape *shape = find_shape_for_type(program, shape_type);
      if (!shape) {
        return set_diag_detail(diag, 3027, "field assignment requires a shape value", target->line, target->column, "mutable shape field", base_type, "assign only to fields of mutable shape values");
      }
      const Param *field = find_shape_field(shape, target->text);
      if (!field) {
        char message[256];
        snprintf(message, sizeof(message), "shape '%s' has no field '%s'", shape->name, target->text);
        return set_diag_detail(diag, 3101, message, target->line, target->column, "declared shape field", target->text, "rename the field or update the shape");
      }
      if (type_is_const(field->type)) {
        return set_diag_detail(diag, 3010, "cannot assign to const field", target->line, target->column, "mutable non-const field", field->type, "remove const from the field type or replace the containing value");
      }
      char *field_type = shape_field_type_for_owner(shape, shape_type, field);
      snprintf(out_type, out_type_len, "%s", field_type ? field_type : "Unknown");
      free(field_type);
      set_expr_resolved_type(target, out_type);
      return true;
    }
    case EXPR_INDEX: {
      char root[128];
      char path[256];
      bool local_place = expr_binding_path(target->left, root, sizeof(root), path, sizeof(path)) && scope_has(scope, root);
      if (local_place) {
        if (!check_place_root_available(target->left, scope, root, diag)) return false;
        if (!check_place_index_exprs(program, target->left, scope, diag)) return false;
      } else if (!check_expr(program, target->left, scope, diag)) return false;
      const char *read_base_type = expr_type(program, target->left, scope);
      char element_type[128];
      char mutref_inner[128];
      bool is_mutref_base = named_ref_inner_text(read_base_type, "mutref", mutref_inner, sizeof(mutref_inner));
      bool is_ref_base = named_ref_inner_text(read_base_type, "ref", mutref_inner, sizeof(mutref_inner));
      const char *mutable_base_type = is_mutref_base ? mutref_inner : read_base_type;
      if (is_ref_base) {
        return set_diag_detail(diag, 3010, "cannot assign through indexed ref<T>", target->line, target->column, "MutSpan<T>, mutref<MutSpan<T>>, or mutable fixed array", "ref<T>", "borrow with &mut when indexed mutation is required");
      }
      if (!mutspan_element_text(mutable_base_type, element_type, sizeof(element_type))) {
        char base_type[128];
        if (!check_lvalue_target(program, target->left, scope, diag, base_type, sizeof(base_type))) return false;
        const char *array_base_type = base_type;
        char array_ref_inner[128];
        if (named_ref_inner_text(array_base_type, "mutref", array_ref_inner, sizeof(array_ref_inner))) array_base_type = array_ref_inner;
        if (!fixed_array_type_parts(array_base_type, NULL, 0, element_type, sizeof(element_type))) {
          return set_diag_detail(diag, 3027, "indexed assignment requires a mutable fixed array or MutSpan", target->line, target->column, "mutable [N]T, MutSpan<T>, mutref<[N]T>, or mutref<MutSpan<T>>", base_type, "String indexing is byte-oriented and read-only; assign only through mutable indexed storage");
        }
      }
      if (type_is_const(element_type)) {
        return set_diag_detail(diag, 3010, "cannot assign to const indexed element", target->line, target->column, "mutable non-const element", element_type, "use a mutable element type when indexed mutation is required");
      }
      if (!check_expr_expected(program, target->right, scope, diag, "usize")) return false;
      const char *index_type = expr_type(program, target->right, scope);
      if (!is_int_type(index_type)) {
        return set_diag_detail(diag, 3028, "index expression must be an integer", target->right ? target->right->line : target->line, target->right ? target->right->column : target->column, "integer index", index_type, "use an integer expression such as usize or a checked integer literal");
      }
      snprintf(out_type, out_type_len, "%s", element_type);
      set_expr_resolved_type(target, out_type);
      return true;
    }
    default:
      return set_diag_detail(diag, 3027, "unsupported assignment target", target->line, target->column, "mutable identifier, shape field, or fixed-array index", "unsupported assignment target", "assign through a mutable local lvalue");
  }
}

static bool generic_call_bindings_from_checked_call(const Program *program, const Function *callee, const Expr *call, Scope *scope, const char *return_type, GenericBinding **out_bindings, size_t *out_len) {
  if (out_bindings) *out_bindings = NULL;
  if (out_len) *out_len = 0;
  if (!callee || !function_is_generic(callee) || !call || !out_bindings || !out_len) return false;

  size_t binding_len = callee->type_params.len;
  GenericBinding *bindings = z_checked_calloc(binding_len, sizeof(GenericBinding));
  for (size_t i = 0; i < binding_len; i++) bindings[i].name = callee->type_params.items[i].name;

  const TypeArgVec *type_args = call_type_args(call);
  if (type_args && type_args->len > 0) {
    if (type_args->len != binding_len) {
      free(bindings);
      return false;
    }
    for (size_t i = 0; i < binding_len; i++) {
      bindings[i].type = provenance_context_type_text(type_args->items[i].type, NULL, 0);
    }
    *out_bindings = bindings;
    *out_len = binding_len;
    return true;
  }

  bool ok = true;
  if (return_type && callee->return_type) {
    ok = infer_generic_type_from_pattern(callee, callee->return_type, return_type, bindings, binding_len);
  }
  for (size_t i = 0; ok && i < callee->params.len && i < call->args.len; i++) {
    ok = infer_generic_type_from_pattern(callee, callee->params.items[i].type, expr_type(program, call->args.items[i], scope), bindings, binding_len);
  }
  if (!ok) {
    generic_bindings_free(bindings, binding_len);
    free(bindings);
    return false;
  }
  *out_bindings = bindings;
  *out_len = binding_len;
  return true;
}

static char *call_param_type_text(const Function *callee, size_t param_index, GenericBinding *bindings, size_t binding_len) {
  if (!callee || param_index >= callee->params.len) return z_strdup("Unknown");
  const char *param_type = callee->params.items[param_index].type;
  if (bindings && binding_len > 0) return type_substitute_generic(param_type, bindings, binding_len);
  return z_strdup(param_type ? param_type : "Unknown");
}

static bool build_constrained_interface_bindings_for_provenance(const Program *program, const Function *context_fun, const Expr *callee, const InterfaceDecl *interface, GenericBinding *context_bindings, size_t context_binding_len, GenericBinding **out_bindings, size_t *out_len) {
  if (out_bindings) *out_bindings = NULL;
  if (out_len) *out_len = 0;
  if (!program || !context_fun || !callee || !interface || !out_bindings || !out_len ||
      !callee->left || callee->left->kind != EXPR_IDENT) return false;
  char **constraint_args = NULL;
  size_t constraint_arg_len = 0;
  constrained_interface_for_type_param(program, context_fun, callee->left->text, &constraint_args, &constraint_arg_len);
  GenericBinding *bindings = z_checked_calloc(interface->type_params.len, sizeof(GenericBinding));
  for (size_t i = 0; i < interface->type_params.len; i++) {
    bindings[i].name = interface->type_params.items[i].name;
    bindings[i].type = provenance_context_type_text(i < constraint_arg_len ? constraint_args[i] : "Unknown", context_bindings, context_binding_len);
  }
  free_type_arg_list(constraint_args, constraint_arg_len);
  *out_bindings = bindings;
  *out_len = interface->type_params.len;
  return true;
}

static bool resolve_provenance_call(const Program *program, const Expr *call, Scope *scope, const char *expected_return, const Function *context_fun, GenericBinding *context_bindings, size_t context_binding_len, ResolvedProvenanceCall *out) {
  if (!out) return false;
  *out = (ResolvedProvenanceCall){0};
  if (!program || !call || call->kind != EXPR_CALL || !call->left || !scope) return false;
  context_fun = context_fun ? context_fun : checking_function;
  const char *return_type = expected_return ? expected_return : expr_type(program, call, scope);

  if (call->left->kind == EXPR_IDENT) {
    const Function *callee = find_function(program, call->left->text);
    if (!callee) return false;
    out->kind = PROVENANCE_CALL_FUNCTION;
    out->callee = callee;
    out->call = call;
    out->return_type = z_strdup(return_type ? return_type : (callee->return_type ? callee->return_type : "Void"));
    if (function_is_generic(callee)) {
      if (!generic_call_bindings_from_checked_call(program, callee, call, scope, out->return_type, &out->bindings, &out->binding_len)) {
        resolved_provenance_call_free(out);
        return false;
      }
      provenance_context_substitute_bindings(out->bindings, out->binding_len, context_bindings, context_binding_len);
    }
    return true;
  }

  if (call->left->kind != EXPR_MEMBER) return false;

  const Shape *shape = NULL;
  const Function *callee = find_namespace_shape_method(program, call->left, &shape);
  if (callee) {
    out->kind = PROVENANCE_CALL_SHAPE_NAMESPACE;
    out->callee = callee;
    out->call = call;
    out->shape = shape;
    out->return_type = z_strdup(return_type ? return_type : (callee->return_type ? callee->return_type : "Void"));
    ZDiag ignored = {0};
    if (!build_shape_method_bindings(program, shape, callee, call, scope, out->return_type, &ignored, &out->bindings, &out->binding_len)) {
      resolved_provenance_call_free(out);
      return false;
    }
    provenance_context_substitute_bindings(out->bindings, out->binding_len, context_bindings, context_binding_len);
    return true;
  }

  shape = NULL;
  callee = find_concrete_constrained_shape_method_in_context(program, context_fun, context_bindings, context_binding_len, call->left, &shape);
  if (callee) {
    out->kind = PROVENANCE_CALL_CONCRETE_CONSTRAINED_SHAPE;
    out->callee = callee;
    out->call = call;
    out->shape = shape;
    out->return_type = z_strdup(return_type ? return_type : (callee->return_type ? callee->return_type : "Void"));
    ZDiag ignored = {0};
    if (!build_shape_method_bindings(program, shape, callee, call, scope, out->return_type, &ignored, &out->bindings, &out->binding_len)) {
      resolved_provenance_call_free(out);
      return false;
    }
    provenance_context_substitute_bindings(out->bindings, out->binding_len, context_bindings, context_binding_len);
    return true;
  }

  const InterfaceDecl *interface = NULL;
  callee = find_constrained_interface_method_in_function(program, context_fun, call->left, &interface);
  if (callee && interface) {
    out->kind = PROVENANCE_CALL_CONSTRAINED_INTERFACE;
    out->callee = callee;
    out->call = call;
    out->interface = interface;
    if (!build_constrained_interface_bindings_for_provenance(program, context_fun, call->left, interface, context_bindings, context_binding_len, &out->bindings, &out->binding_len)) {
      resolved_provenance_call_free(out);
      return false;
    }
    char *substituted_return = type_substitute_generic(callee->return_type, out->bindings, out->binding_len);
    out->return_type = z_strdup(return_type ? return_type : substituted_return);
    free(substituted_return);
    return true;
  }

  const Expr *receiver = call->left->left;
  if (!receiver) return false;
  const char *receiver_type_raw = expr_type(program, receiver, scope);
  char receiver_type[192];
  strip_ref_like_type(receiver_type_raw, receiver_type, sizeof(receiver_type));
  const Shape *receiver_shape = find_shape_for_type(program, receiver_type);
  callee = find_shape_method_decl(receiver_shape, call->left->text);
  bool receiver_requires_mut = false;
  if (!callee || !shape_method_receiver_info(callee, &receiver_requires_mut)) return false;
  out->kind = PROVENANCE_CALL_RECEIVER;
  out->callee = callee;
  out->call = call;
  out->receiver = receiver;
  out->param_offset = 1;
  out->shape = receiver_shape;
  out->return_type = z_strdup(return_type ? return_type : (callee->return_type ? callee->return_type : "Void"));
  ZDiag ignored = {0};
  char *self_arg_type = receiver_self_arg_type(expr_type(program, receiver, scope), receiver_requires_mut);
  if (!build_receiver_shape_method_bindings(program, receiver_shape, callee, call, self_arg_type, scope, &ignored, &out->bindings, &out->binding_len)) {
    free(self_arg_type);
    resolved_provenance_call_free(out);
    return false;
  }
  free(self_arg_type);
  provenance_context_substitute_bindings(out->bindings, out->binding_len, context_bindings, context_binding_len);
  return true;
}

static bool function_return_value_provenance(const Program *program, const Function *fun, GenericBinding *bindings, size_t binding_len, ValueProvenance *origins, bool *may_return);
static bool function_param_index_by_name(const Function *fun, const char *name, size_t *out_index);

static bool type_value_provenance_from_place(
  const Program *program,
  const char *type,
  Scope *scope,
  const char *root,
  Scope *root_scope,
  const char *root_path,
  const char *value_path,
  ValueProvenance *origins,
  size_t depth
) {
  if (!program || !type || !root || !origins || depth > 16) return false;
  bool local_storage = reference_place_origin_is_local_storage(scope, root);
  if (type_is_named_generic(type, "ref") || type_is_named_generic(type, "mutref")) {
    return value_provenance_add_full(origins, root, root_scope ? root_scope : scope_binding_scope(scope, root), type_is_named_generic(type, "mutref"), local_storage, value_path, root_path);
  }
  const char *inner = NULL;
  size_t inner_len = 0;
  if (type_has_generic_arg(type, "Maybe", &inner, &inner_len) ||
      type_has_generic_arg(type, "owned", &inner, &inner_len)) {
    char *inner_type = z_strndup(inner, inner_len);
    const char *prefix = type_has_generic_arg(type, "Maybe", &inner, &inner_len) ? "value" : NULL;
    char *next_value_path = origin_path_join(value_path, prefix);
    char *next_root_path = origin_path_join(root_path, prefix);
    bool added = type_value_provenance_from_place(program, inner_type, scope, root, root_scope, next_root_path, next_value_path, origins, depth + 1);
    free(next_value_path);
    free(next_root_path);
    free(inner_type);
    return added;
  }
  char element_type[160];
  if (fixed_array_type_parts(type, NULL, 0, element_type, sizeof(element_type))) {
    char *next_value_path = origin_path_join(value_path, "[*]");
    char *next_root_path = origin_path_join(root_path, "[*]");
    bool added = type_value_provenance_from_place(program, element_type, scope, root, root_scope, next_root_path, next_value_path, origins, depth + 1);
    free(next_value_path);
    free(next_root_path);
    return added;
  }
  const Choice *choice = find_choice(program, type);
  if (choice) {
    bool added = false;
    for (size_t i = 0; i < choice->cases.len; i++) {
      const Param *item_case = &choice->cases.items[i];
      if (!item_case->name || !item_case->type) continue;
      char *next_value_path = origin_path_join(value_path, item_case->name);
      char *next_root_path = origin_path_join(root_path, item_case->name);
      if (type_value_provenance_from_place(program, item_case->type, scope, root, root_scope, next_root_path, next_value_path, origins, depth + 1)) added = true;
      free(next_value_path);
      free(next_root_path);
    }
    return added;
  }
  const Shape *shape = find_shape_for_type(program, type);
  if (shape) {
    bool added = false;
    for (size_t i = 0; i < shape->fields.len; i++) {
      const Param *field = &shape->fields.items[i];
      if (!field->name || !field->type) continue;
      char *field_type = shape_field_type_for_owner(shape, type, field);
      char *next_value_path = origin_path_join(value_path, field->name);
      char *next_root_path = origin_path_join(root_path, field->name);
      if (type_value_provenance_from_place(program, field_type, scope, root, root_scope, next_root_path, next_value_path, origins, depth + 1)) added = true;
      free(next_value_path);
      free(next_root_path);
      free(field_type);
    }
    return added;
  }
  return false;
}

static bool maybe_unwrapped_value_provenance(ValueProvenance *out, const ValueProvenance *source) {
  return value_provenance_add_all_under_path(out, source, "value");
}

static bool choice_constructor_value_provenance(const Program *program, const Expr *expr, Scope *scope, ValueProvenance *origins) {
  if (!program || !expr || expr->kind != EXPR_CALL || !expr->left || expr->left->kind != EXPR_MEMBER || !origins) return false;
  const Expr *owner = expr->left->left;
  if (!owner || owner->kind != EXPR_IDENT) return false;
  const Choice *choice = find_choice(program, owner->text);
  if (!choice) return false;
  const Param *item_case = find_case(&choice->cases, expr->left->text);
  if (!item_case || !item_case->type || expr->args.len != 1) return false;

  ValueProvenance payload_origins = {0};
  bool added = false;
  if (expr_reference_provenance(program, expr->args.items[0], scope, &payload_origins)) {
    added = value_provenance_add_all_with_prefix(origins, &payload_origins, item_case->name);
  }
  value_provenance_free(&payload_origins);
  return added;
}

static void register_match_payload_binding_provenance(
  const Program *program,
  const Expr *match_expr,
  Scope *match_scope,
  Scope *payload_scope,
  const char *payload_name,
  const char *case_name
) {
  if (!program || !match_expr || !match_scope || !payload_scope || !payload_name || !case_name) return;
  ValueProvenance match_origins = {0};
  if (!expr_reference_provenance(program, match_expr, match_scope, &match_origins)) {
    value_provenance_free(&match_origins);
    return;
  }
  ValueProvenance payload_origins = {0};
  if (value_provenance_add_all_under_path(&payload_origins, &match_origins, case_name)) {
    scope_set_value_provenance(payload_scope, payload_name, &payload_origins);
  }
  value_provenance_free(&payload_origins);
  value_provenance_free(&match_origins);
}

static bool std_mem_get_value_provenance(const Program *program, const Expr *expr, Scope *scope, ValueProvenance *origins) {
  if (!program || !expr || expr->kind != EXPR_CALL || !expr->left || expr->left->kind != EXPR_MEMBER || expr->args.len < 1) return false;
  ZBuf callee_name;
  zbuf_init(&callee_name);
  member_name_buf(expr->left, &callee_name);
  bool is_get = strcmp(callee_name.data, "std.mem.get") == 0;
  zbuf_free(&callee_name);
  if (!is_get) return false;

  const Expr *collection = expr->args.items[0];
  bool added = false;
  ValueProvenance collection_provenance = {0};
  if (expr_reference_provenance(program, collection, scope, &collection_provenance)) {
    ValueProvenance element_provenance = {0};
    if (value_provenance_add_all_under_path(&element_provenance, &collection_provenance, "[*]")) {
      if (value_provenance_add_all_with_prefix(origins, &element_provenance, "value")) added = true;
    }
    value_provenance_free(&element_provenance);
  }
  value_provenance_free(&collection_provenance);
  if (added) return true;

  char root[128];
  char path[256];
  if (!expr_binding_path(collection, root, sizeof(root), path, sizeof(path)) || !scope_has(scope, root)) return false;
  char element_type[160];
  if (!index_element_type(expr_type(program, collection, scope), element_type, sizeof(element_type))) return false;
  char *element_path = origin_path_join(path, "[*]");
  added = type_value_provenance_from_place(program, element_type, scope, root, scope_binding_scope(scope, root), element_path, "value", origins, 0);
  free(element_path);
  return added;
}

static bool value_provenance_add_actual_place(ValueProvenance *origins, const Expr *actual, Scope *scope, const ProvenanceEntry *summary_entry) {
  if (!origins || !actual || !scope || !summary_entry) return false;
  char root[128];
  char path[256];
  if (!expr_binding_path(actual, root, sizeof(root), path, sizeof(path)) || !scope_has(scope, root)) return false;
  char *origin_path = origin_path_join(path, summary_entry->origin.path);
  bool local_storage = borrow_expr_source_is_local_storage(actual, scope, root);
  bool added = value_provenance_add_full(
    origins,
    root,
    scope_binding_scope(scope, root),
    summary_entry->mutable_borrow,
    local_storage,
    summary_entry->value_path,
    origin_path
  );
  free(origin_path);
  return added;
}

static bool instantiate_call_provenance_entry(const Program *program, const Function *callee, const Expr *call, const Expr *receiver, size_t param_offset, Scope *scope, GenericBinding *bindings, size_t binding_len, const ProvenanceEntry *summary_entry, ValueProvenance *out);

static bool call_result_value_provenance(const Program *program, const Expr *expr, Scope *scope, ValueProvenance *origins) {
  if (!program || !expr || expr->kind != EXPR_CALL || !expr->left || !origins) return false;
  if (std_mem_get_value_provenance(program, expr, scope, origins)) return true;
  const char *return_type = expr_type(program, expr, scope);
  bool return_mut = type_is_named_generic(return_type, "mutref");

  ResolvedProvenanceCall resolved = {0};
  if (!resolve_provenance_call(program, expr, scope, return_type, checking_function, return_provenance_expr_bindings, return_provenance_expr_binding_len, &resolved)) return false;

  bool added = false;
  ValueProvenance summary = {0};
  bool callee_may_return = true;
  if (function_return_value_provenance(program, resolved.callee, resolved.bindings, resolved.binding_len, &summary, &callee_may_return)) {
    for (size_t origin_index = 0; origin_index < summary.len; origin_index++) {
      if (instantiate_call_provenance_entry(program, resolved.callee, expr, resolved.receiver, resolved.param_offset, scope, resolved.bindings, resolved.binding_len, &summary.items[origin_index], origins)) added = true;
    }
  }
  value_provenance_free(&summary);
  if (added) {
    resolved_provenance_call_free(&resolved);
    return true;
  }
  if (!callee_may_return) {
    resolved_provenance_call_free(&resolved);
    return false;
  }

  if (!return_mut && !type_is_named_generic(return_type, "ref")) {
    resolved_provenance_call_free(&resolved);
    return false;
  }

  if (resolved.receiver && resolved.callee->params.len > 0) {
    char *param_type = call_param_type_text(resolved.callee, 0, resolved.bindings, resolved.binding_len);
    if (type_is_named_generic(param_type, "ref") || type_is_named_generic(param_type, "mutref")) {
      if (expr_reference_provenance_as(program, resolved.receiver, scope, origins, return_mut)) {
        added = true;
      } else {
        char root[128];
        if (expr_root_ident(resolved.receiver, root, sizeof(root)) && scope_has(scope, root)) {
          if (value_provenance_add(origins, root, scope_binding_scope(scope, root), return_mut, reference_source_origin_is_local_storage(scope, root))) added = true;
        }
      }
    }
    free(param_type);
  }

  for (size_t i = 0; i < expr->args.len && i + resolved.param_offset < resolved.callee->params.len; i++) {
    char *param_type = call_param_type_text(resolved.callee, i + resolved.param_offset, resolved.bindings, resolved.binding_len);
    bool reference_param = type_is_named_generic(param_type, "ref") || type_is_named_generic(param_type, "mutref");
    free(param_type);
    if (!reference_param) continue;
    if (expr_reference_provenance_as(program, expr->args.items[i], scope, origins, return_mut)) added = true;
  }
  resolved_provenance_call_free(&resolved);
  return added;
}

static bool expr_reference_provenance(const Program *program, const Expr *expr, Scope *scope, ValueProvenance *origins) {
  if (!expr || !origins) return false;
  if (expr_value_provenance(expr, scope, origins) ||
      choice_constructor_value_provenance(program, expr, scope, origins) ||
      call_result_value_provenance(program, expr, scope, origins)) return true;
  if (expr->kind == EXPR_IDENT) {
    const char *actual = scope_type(scope, expr->text);
    if (actual && type_value_provenance_from_place(program, actual, scope, expr->text, scope_binding_scope(scope, expr->text), NULL, NULL, origins, 0)) return true;
  }
  if (expr->kind == EXPR_CHECK) {
    ValueProvenance checked_provenance = {0};
    bool added = false;
    if (expr_reference_provenance(program, expr->left, scope, &checked_provenance)) {
      const char *checked_type = expr_type(program, expr->left, scope);
      const char *inner = NULL;
      size_t inner_len = 0;
      if (type_has_generic_arg(checked_type, "Maybe", &inner, &inner_len)) {
        added = maybe_unwrapped_value_provenance(origins, &checked_provenance);
      } else {
        added = value_provenance_add_all(origins, &checked_provenance);
      }
    }
    value_provenance_free(&checked_provenance);
    return added;
  }
  if (expr->kind == EXPR_RESCUE) {
    bool added = false;
    ValueProvenance left_provenance = {0};
    if (expr_reference_provenance(program, expr->left, scope, &left_provenance)) {
      const char *left_type = expr_type(program, expr->left, scope);
      const char *inner = NULL;
      size_t inner_len = 0;
      if (type_has_generic_arg(left_type, "Maybe", &inner, &inner_len)) {
        if (maybe_unwrapped_value_provenance(origins, &left_provenance)) added = true;
      } else if (value_provenance_add_all(origins, &left_provenance)) {
        added = true;
      }
    }
    value_provenance_free(&left_provenance);
    ValueProvenance fallback_provenance = {0};
    if (expr_reference_provenance(program, expr->right, scope, &fallback_provenance)) {
      if (value_provenance_add_all(origins, &fallback_provenance)) added = true;
    }
    value_provenance_free(&fallback_provenance);
    return added;
  }
  if (expr->kind == EXPR_CAST) return expr_reference_provenance(program, expr->left, scope, origins);
  if (expr->kind == EXPR_MEMBER) {
    char root[128];
    char path[256];
    const char *member_type = expr_type(program, expr, scope);
    if (expr_binding_path(expr, root, sizeof(root), path, sizeof(path))) {
      ValueProvenance binding_origins = {0};
      bool has_binding_origins = scope_copy_value_provenance(scope, root, &binding_origins);
      bool added = value_provenance_add_all_under_path(origins, &binding_origins, path);
      const char *root_type = scope_type(scope, root);
      if (!added && has_binding_origins && origin_path_text(path)[0] &&
          (type_is_named_generic(root_type, "ref") || type_is_named_generic(root_type, "mutref"))) {
        PlaceVec places = {0};
        for (size_t i = 0; i < binding_origins.len; i++) {
          ProvenanceEntry *entry = &binding_origins.items[i];
          if (origin_path_text(entry->value_path)[0]) continue;
          char *place_path = origin_path_join(entry->origin.path, path);
          place_vec_add(&places, entry->origin.root, entry->origin.root_scope, place_path);
          free(place_path);
        }
        for (size_t i = 0; i < places.len; i++) {
          if (place_storage_value_provenance_under_path(program, scope, &places.items[i], NULL, origins)) added = true;
        }
        place_vec_free(&places);
      }
      value_provenance_free(&binding_origins);
      if (added) return true;
      if (scope_has(scope, root) && type_value_provenance_from_place(program, member_type, scope, root, scope_binding_scope(scope, root), path, NULL, origins, 0)) return true;
      if (has_binding_origins) return false;
    }
    if (type_is_named_generic(member_type, "ref") || type_is_named_generic(member_type, "mutref")) {
      ValueProvenance receiver_origins = {0};
      bool added = false;
      if (expr_reference_provenance(program, expr->left, scope, &receiver_origins)) {
        added = value_provenance_add_all_under_path(origins, &receiver_origins, expr->text);
      }
      value_provenance_free(&receiver_origins);
      return added;
    }
  }
  if (expr->kind == EXPR_INDEX) {
    char root[128];
    char path[256];
    const char *element_type = expr_type(program, expr, scope);
    if (expr_binding_path(expr, root, sizeof(root), path, sizeof(path))) {
      ValueProvenance binding_origins = {0};
      bool has_binding_origins = scope_copy_value_provenance(scope, root, &binding_origins);
      bool added = value_provenance_add_all_under_path(origins, &binding_origins, path);
      const char *root_type = scope_type(scope, root);
      if (!added && has_binding_origins && origin_path_text(path)[0] &&
          (type_is_named_generic(root_type, "ref") || type_is_named_generic(root_type, "mutref"))) {
        PlaceVec places = {0};
        for (size_t i = 0; i < binding_origins.len; i++) {
          ProvenanceEntry *entry = &binding_origins.items[i];
          if (origin_path_text(entry->value_path)[0]) continue;
          char *place_path = origin_path_join(entry->origin.path, path);
          place_vec_add(&places, entry->origin.root, entry->origin.root_scope, place_path);
          free(place_path);
        }
        for (size_t i = 0; i < places.len; i++) {
          if (place_storage_value_provenance_under_path(program, scope, &places.items[i], NULL, origins)) added = true;
        }
        place_vec_free(&places);
      }
      value_provenance_free(&binding_origins);
      if (added) return true;
      if (scope_has(scope, root) && type_value_provenance_from_place(program, element_type, scope, root, scope_binding_scope(scope, root), path, NULL, origins, 0)) return true;
      if (has_binding_origins) return false;
    }
    if (type_is_named_generic(element_type, "ref") || type_is_named_generic(element_type, "mutref")) {
      ValueProvenance receiver_origins = {0};
      bool added = false;
      if (expr_reference_provenance(program, expr->left, scope, &receiver_origins)) {
        added = value_provenance_add_all_under_path(origins, &receiver_origins, "[*]");
      }
      value_provenance_free(&receiver_origins);
      return added;
    }
  }
  if (expr->kind == EXPR_SHAPE_LITERAL) {
    bool added = false;
    for (size_t i = 0; i < expr->fields.len; i++) {
      ValueProvenance field_origins = {0};
      if (expr_reference_provenance(program, expr->fields.items[i].value, scope, &field_origins)) {
        if (value_provenance_add_all_with_prefix(origins, &field_origins, expr->fields.items[i].name)) added = true;
      }
      value_provenance_free(&field_origins);
    }
    return added;
  }
  if (expr->kind == EXPR_ARRAY_LITERAL) {
    bool added = false;
    size_t element_count = expr->array_repeat ? (expr->args.len > 0 ? 1 : 0) : expr->args.len;
    for (size_t i = 0; i < element_count; i++) {
      ValueProvenance item_origins = {0};
      if (expr_reference_provenance(program, expr->args.items[i], scope, &item_origins)) {
        char element_path[40];
        snprintf(element_path, sizeof(element_path), "[%zu]", i);
        if (value_provenance_add_all_with_prefix(origins, &item_origins, element_path)) added = true;
      }
      value_provenance_free(&item_origins);
    }
    return added;
  }
  return false;
}

static bool register_borrow_binding(const Program *program, const Stmt *stmt, Scope *scope) {
  if (!stmt || !stmt->name || !stmt->expr) return false;
  ValueProvenance origins = {0};
  if (!expr_reference_provenance(program, stmt->expr, scope, &origins)) {
    value_provenance_free(&origins);
    return false;
  }
  const char *binding_type = stmt->resolved_type ? stmt->resolved_type : stmt->type;
  if (binding_type && (type_is_named_generic(binding_type, "ref") || type_is_named_generic(binding_type, "mutref"))) {
    bool mut_borrow = type_is_named_generic(binding_type, "mutref");
    for (size_t i = 0; i < origins.len; i++) origins.items[i].mutable_borrow = mut_borrow;
  }
  scope_set_value_provenance(scope, stmt->name, &origins);
  value_provenance_free(&origins);
  return true;
}

static bool collect_assignment_target_places(const Expr *target, Scope *scope, PlaceVec *places) {
  if (!target || !scope || !places) return false;
  char root[128];
  char path[256];
  if (!expr_binding_path(target, root, sizeof(root), path, sizeof(path)) || !scope_has(scope, root)) return false;

  const char *root_type = scope_type(scope, root);
  bool root_ref_like = type_is_named_generic(root_type, "ref") || type_is_named_generic(root_type, "mutref");
  bool added = false;
  if (root_ref_like && origin_path_text(path)[0]) {
    ValueProvenance root_origins = {0};
    if (scope_copy_value_provenance(scope, root, &root_origins)) {
      for (size_t i = 0; i < root_origins.len; i++) {
        ProvenanceEntry *entry = &root_origins.items[i];
        if (origin_path_text(entry->value_path)[0]) continue;
        char *target_path = origin_path_join(entry->origin.path, path);
        if (place_vec_add(places, entry->origin.root, entry->origin.root_scope, target_path)) added = true;
        free(target_path);
      }
    }
    value_provenance_free(&root_origins);
  }
  if (added) return true;
  return place_vec_add(places, root, scope_binding_scope(scope, root), path);
}

static bool update_borrow_assignment(const Program *program, const Expr *target, const Expr *value, Scope *scope, ZDiag *diag) {
  char target_root[128];
  char target_path[256];
  if (!target || !expr_binding_path(target, target_root, sizeof(target_root), target_path, sizeof(target_path)) || !scope_has(scope, target_root)) return true;
  const char *target_type = target->resolved_type ? target->resolved_type : scope_type(scope, target_root);
  PlaceVec targets = {0};
  if (!collect_assignment_target_places(target, scope, &targets)) return true;
  ValueProvenance origins = {0};
  if (expr_reference_provenance(program, value, scope, &origins)) {
    for (size_t target_index = 0; target_index < targets.len; target_index++) {
      Scope *target_scope = targets.items[target_index].root_scope ? targets.items[target_index].root_scope : scope_binding_scope(scope, targets.items[target_index].root);
      for (size_t i = 0; i < origins.len; i++) {
        ProvenanceEntry *entry = &origins.items[i];
        Scope *root_scope = entry->origin.root_scope ? entry->origin.root_scope : scope_binding_scope(scope, entry->origin.root);
        if (target_scope && root_scope && !scope_is_ancestor_or_self(root_scope, target_scope)) {
          char actual[256];
          snprintf(actual, sizeof(actual), "reference to shorter-lived local '%s'", entry->origin.root);
          value_provenance_free(&origins);
          place_vec_free(&targets);
          return set_diag_detail(diag, 3030, "cannot assign a reference to a shorter-lived binding", value ? value->line : target->line, value ? value->column : target->column, "borrow source that outlives the reference binding", actual, "keep the reference binding in the same lexical scope as the borrowed value");
        }
      }
    }
    if (type_is_named_generic(target_type, "ref") || type_is_named_generic(target_type, "mutref")) {
      bool mut_borrow = type_is_named_generic(target_type, "mutref");
      for (size_t i = 0; i < origins.len; i++) origins.items[i].mutable_borrow = mut_borrow;
    }
    for (size_t target_index = 0; target_index < targets.len; target_index++) {
      Place *place = &targets.items[target_index];
      scope_set_value_provenance_path_in_scope(scope, place->root_scope, place->root, place->path, &origins);
    }
  } else {
    ValueProvenance empty = {0};
    for (size_t target_index = 0; target_index < targets.len; target_index++) {
      Place *place = &targets.items[target_index];
      scope_set_value_provenance_path_in_scope(scope, place->root_scope, place->root, place->path, &empty);
    }
  }
  value_provenance_free(&origins);
  place_vec_free(&targets);
  return true;
}

typedef struct {
  PlaceVec targets;
  ValueProvenance *previous;
} AssignmentProvenanceSnapshot;

static void assignment_provenance_snapshot_clear(const Expr *target, Scope *scope, AssignmentProvenanceSnapshot *snapshot) {
  if (!snapshot) return;
  *snapshot = (AssignmentProvenanceSnapshot){0};
  if (!target || !scope) return;
  if (!collect_assignment_target_places(target, scope, &snapshot->targets)) return;
  snapshot->previous = z_checked_calloc(snapshot->targets.len, sizeof(ValueProvenance));
  for (size_t i = 0; i < snapshot->targets.len; i++) {
    Place *place = &snapshot->targets.items[i];
    ValueProvenance full = {0};
    if (scope_copy_value_provenance_from_scope(scope, place->root_scope, place->root, &full)) {
      value_provenance_add_all_under_path(&snapshot->previous[i], &full, place->path);
    }
    value_provenance_free(&full);
    ValueProvenance empty = {0};
    scope_set_value_provenance_path_in_scope(scope, place->root_scope, place->root, place->path, &empty);
  }
}

static void assignment_provenance_snapshot_restore(Scope *scope, AssignmentProvenanceSnapshot *snapshot) {
  if (!snapshot) return;
  for (size_t i = 0; i < snapshot->targets.len; i++) {
    Place *place = &snapshot->targets.items[i];
    scope_set_value_provenance_path_in_scope(scope, place->root_scope, place->root, place->path, &snapshot->previous[i]);
    value_provenance_free(&snapshot->previous[i]);
  }
  free(snapshot->previous);
  snapshot->previous = NULL;
  place_vec_free(&snapshot->targets);
}

static size_t function_return_provenance_depth = 0;

static char *return_provenance_type_text(const char *type, GenericBinding *bindings, size_t binding_len) {
  if (!type) return NULL;
  if (bindings && binding_len > 0) return type_substitute_generic(type, bindings, binding_len);
  return z_strdup(type);
}

static bool collect_return_value_provenance_from_stmt_vec(const Program *program, const Function *fun, const StmtVec *body, Scope *scope, GenericBinding *bindings, size_t binding_len, ValueProvenance *out, bool *may_return, bool *complete) {
  if (!program || !fun || !body || !scope || !out) {
    if (complete) *complete = false;
    return false;
  }
  bool added = false;
  for (size_t stmt_index = 0; stmt_index < body->len; stmt_index++) {
    const Stmt *stmt = body->items[stmt_index];
    if (!stmt) continue;
    if (stmt->kind == STMT_LET) {
      ZDiag ignored = {0};
      if (!apply_expr_call_storage_effects(program, stmt->expr, scope, &ignored)) {
        if (complete) *complete = false;
        return added;
      }
      const char *binding_type = stmt->resolved_type ? stmt->resolved_type : stmt->type;
      if (!binding_type && stmt->expr) binding_type = expr_type(program, stmt->expr, scope);
      char *substituted_type = return_provenance_type_text(binding_type, bindings, binding_len);
      if (stmt->name) {
        scope_add(scope, stmt->name, substituted_type ? substituted_type : "Unknown", stmt->mutable_binding);
        register_borrow_binding(program, stmt, scope);
      }
      free(substituted_type);
      continue;
    }
    if (stmt->kind == STMT_ASSIGN) {
      ZDiag ignored = {0};
      if (!apply_expr_call_storage_effects(program, stmt->expr, scope, &ignored)) {
        if (complete) *complete = false;
        return added;
      }
      if (!update_borrow_assignment(program, stmt->target, stmt->expr, scope, &ignored)) {
        if (complete) *complete = false;
        return added;
      }
      continue;
    }
    if (stmt->kind == STMT_RETURN) {
      ZDiag ignored = {0};
      if (!apply_expr_call_storage_effects(program, stmt->expr, scope, &ignored)) {
        if (complete) *complete = false;
        return added;
      }
      if (may_return) *may_return = true;
      ValueProvenance origins = {0};
      if (expr_reference_provenance(program, stmt->expr, scope, &origins)) {
        if (value_provenance_add_all(out, &origins)) added = true;
      }
      value_provenance_free(&origins);
      return added;
    }
    if (stmt->kind == STMT_RAISE) {
      if (fun->raises) return added;
      continue;
    }
    if (stmt->kind == STMT_EXPR || stmt->kind == STMT_CHECK || stmt->kind == STMT_DEFER) {
      ZDiag ignored = {0};
      if (!apply_expr_call_storage_effects(program, stmt->expr, scope, &ignored)) {
        if (complete) *complete = false;
        return added;
      }
      continue;
    }
    if (stmt->kind == STMT_IF) {
      ZDiag ignored = {0};
      if (!apply_expr_call_storage_effects(program, stmt->expr, scope, &ignored)) {
        if (complete) *complete = false;
        return added;
      }
      bool then_possible = true;
      bool else_possible = true;
      if (stmt->expr && stmt->expr->kind == EXPR_BOOL) {
        then_possible = stmt->expr->bool_value;
        else_possible = !stmt->expr->bool_value;
      }
      ProvenanceScopeSnapshot *before = provenance_scope_snapshot_capture(scope);
      ProvenanceScopeSnapshot *then_after = NULL;
      ProvenanceScopeSnapshot *else_after = NULL;
      if (then_possible) {
        Scope then_scope = {.parent = scope};
        if (collect_return_value_provenance_from_stmt_vec(program, fun, &stmt->then_body, &then_scope, bindings, binding_len, out, may_return, complete)) added = true;
        scope_free(&then_scope);
        then_after = provenance_scope_snapshot_capture(scope);
      }
      provenance_scope_snapshot_restore(before);
      if (else_possible) {
        Scope else_scope = {.parent = scope};
        if (collect_return_value_provenance_from_stmt_vec(program, fun, &stmt->else_body, &else_scope, bindings, binding_len, out, may_return, complete)) added = true;
        scope_free(&else_scope);
        else_after = provenance_scope_snapshot_capture(scope);
      }
      ProvenanceScopeSnapshot *states[] = {then_after, else_after};
      bool continues[] = {
        then_possible && !stmt_vec_guarantees_exit(&stmt->then_body, fun->raises),
        else_possible && !stmt_vec_guarantees_exit(&stmt->else_body, fun->raises),
      };
      provenance_scope_snapshot_restore_union(before, states, continues, 2);
      provenance_scope_snapshot_free(then_after);
      provenance_scope_snapshot_free(else_after);
      provenance_scope_snapshot_free(before);
      if (!continues[0] && !continues[1]) return added;
      continue;
    }
    if (stmt->kind == STMT_WHILE) {
      ZDiag ignored = {0};
      if (!apply_expr_call_storage_effects(program, stmt->expr, scope, &ignored)) {
        if (complete) *complete = false;
        return added;
      }
      ProvenanceScopeSnapshot *before = provenance_scope_snapshot_capture(scope);
      bool body_possible = !(stmt->expr && stmt->expr->kind == EXPR_BOOL && !stmt->expr->bool_value);
      ProvenanceScopeSnapshot *body_after = NULL;
      if (body_possible) {
        Scope body_scope = {.parent = scope};
        if (collect_return_value_provenance_from_stmt_vec(program, fun, &stmt->then_body, &body_scope, bindings, binding_len, out, may_return, complete)) added = true;
        scope_free(&body_scope);
        body_after = provenance_scope_snapshot_capture(scope);
      }
      ProvenanceScopeSnapshot *states[] = {before, body_after};
      bool continues[] = {true, body_possible && !stmt_vec_guarantees_exit(&stmt->then_body, fun->raises)};
      provenance_scope_snapshot_restore_union(before, states, continues, 2);
      provenance_scope_snapshot_free(body_after);
      provenance_scope_snapshot_free(before);
      continue;
    }
    if (stmt->kind == STMT_FOR) {
      ZDiag ignored = {0};
      if (!apply_expr_call_storage_effects(program, stmt->expr, scope, &ignored) ||
          !apply_expr_call_storage_effects(program, stmt->range_end, scope, &ignored)) {
        if (complete) *complete = false;
        return added;
      }
      ProvenanceScopeSnapshot *before = provenance_scope_snapshot_capture(scope);
      Scope body_scope = {.parent = scope};
      const char *iter_type = stmt->resolved_type ? stmt->resolved_type : (stmt->expr ? expr_type(program, stmt->expr, scope) : "Unknown");
      char *substituted_iter_type = return_provenance_type_text(iter_type, bindings, binding_len);
      if (stmt->name) scope_add(&body_scope, stmt->name, substituted_iter_type ? substituted_iter_type : "Unknown", false);
      free(substituted_iter_type);
      if (collect_return_value_provenance_from_stmt_vec(program, fun, &stmt->then_body, &body_scope, bindings, binding_len, out, may_return, complete)) added = true;
      scope_free(&body_scope);
      ProvenanceScopeSnapshot *body_after = provenance_scope_snapshot_capture(scope);
      ProvenanceScopeSnapshot *states[] = {before, body_after};
      bool continues[] = {true, !stmt_vec_guarantees_exit(&stmt->then_body, fun->raises)};
      provenance_scope_snapshot_restore_union(before, states, continues, 2);
      provenance_scope_snapshot_free(body_after);
      provenance_scope_snapshot_free(before);
      continue;
    }
    if (stmt->kind == STMT_MATCH) {
      ZDiag ignored = {0};
      if (!apply_expr_call_storage_effects(program, stmt->expr, scope, &ignored)) {
        if (complete) *complete = false;
        return added;
      }
      ProvenanceScopeSnapshot *before = provenance_scope_snapshot_capture(scope);
      ProvenanceScopeSnapshot **arm_states = z_checked_calloc(stmt->match_arms.len, sizeof(ProvenanceScopeSnapshot *));
      bool *arm_continues = z_checked_calloc(stmt->match_arms.len, sizeof(bool));
      const char *match_type = stmt->resolved_type ? stmt->resolved_type : (stmt->expr ? expr_type(program, stmt->expr, scope) : "Unknown");
      const Choice *item_choice = find_choice(program, match_type);
      for (size_t arm_index = 0; arm_index < stmt->match_arms.len; arm_index++) {
        provenance_scope_snapshot_restore(before);
        const MatchArm *arm = &stmt->match_arms.items[arm_index];
        Scope arm_scope = {.parent = scope};
        if (arm->payload_name && item_choice) {
          const Param *item_case = find_case(&item_choice->cases, arm->case_name);
          if (item_case && item_case->type) {
            char *payload_type = return_provenance_type_text(item_case->type, bindings, binding_len);
            scope_add(&arm_scope, arm->payload_name, payload_type ? payload_type : "Unknown", false);
            register_match_payload_binding_provenance(program, stmt->expr, scope, &arm_scope, arm->payload_name, arm->case_name);
            free(payload_type);
          }
        }
        if (collect_return_value_provenance_from_stmt_vec(program, fun, &arm->body, &arm_scope, bindings, binding_len, out, may_return, complete)) added = true;
        scope_free(&arm_scope);
        arm_states[arm_index] = provenance_scope_snapshot_capture(scope);
        arm_continues[arm_index] = !stmt_vec_guarantees_exit(&arm->body, fun->raises);
      }
      provenance_scope_snapshot_restore_union(before, arm_states, arm_continues, stmt->match_arms.len);
      bool any_arm_continues = false;
      for (size_t i = 0; i < stmt->match_arms.len; i++) {
        if (arm_continues[i]) any_arm_continues = true;
      }
      for (size_t i = 0; i < stmt->match_arms.len; i++) provenance_scope_snapshot_free(arm_states[i]);
      free(arm_states);
      free(arm_continues);
      provenance_scope_snapshot_free(before);
      if (!any_arm_continues) return added;
      continue;
    }
  }
  return added;
}

static bool seed_param_storage_value_provenance(const Program *program, Scope *scope, const char *name, const char *type) {
  if (!program || !scope || !name || !type) return false;
  const char *storage_type = type;
  char ref_inner[192];
  if (named_ref_inner_text(type, "ref", ref_inner, sizeof(ref_inner)) ||
      named_ref_inner_text(type, "mutref", ref_inner, sizeof(ref_inner))) {
    storage_type = ref_inner;
  }
  ValueProvenance seed = {0};
  bool added = type_value_provenance_from_place(program, storage_type, scope, name, scope_binding_scope(scope, name), NULL, NULL, &seed, 0);
  if (added) scope_set_value_provenance(scope, name, &seed);
  value_provenance_free(&seed);
  return added;
}

static bool function_provenance_summary(const Program *program, const Function *fun, GenericBinding *bindings, size_t binding_len, FunctionProvenanceSummary *summary) {
  if (!summary) return false;
  *summary = (FunctionProvenanceSummary){.may_return = true};
  if (!program || !fun) return false;
  if (function_return_provenance_depth > 16) return false;
  summary->may_return = false;
  summary->return_complete = true;
  summary->effect_complete = true;
  function_return_provenance_depth++;
  GenericBinding *previous_expr_bindings = return_provenance_expr_bindings;
  size_t previous_expr_binding_len = return_provenance_expr_binding_len;
  const Function *previous_checking_function = checking_function;
  return_provenance_expr_bindings = bindings;
  return_provenance_expr_binding_len = binding_len;
  checking_function = fun;
  Scope scope = {0};
  for (size_t param_index = 0; param_index < fun->params.len; param_index++) {
    const Param *param = &fun->params.items[param_index];
    if (param->name) {
      char *param_type = return_provenance_type_text(param->type, bindings, binding_len);
      scope_add_param_decl(&scope, param->name, param_type ? param_type : "Unknown", param->line, param->column);
      if (type_is_named_generic(param_type, "mutref")) {
        seed_param_storage_value_provenance(program, &scope, param->name, param_type ? param_type : "Unknown");
      }
      free(param_type);
    }
  }

  bool body_complete = true;
  collect_return_value_provenance_from_stmt_vec(program, fun, &fun->body, &scope, bindings, binding_len, &summary->return_value, &summary->may_return, &body_complete);
  if (!body_complete) {
    summary->return_complete = false;
    summary->effect_complete = false;
  }
  for (size_t param_index = 0; param_index < fun->params.len; param_index++) {
    const Param *param = &fun->params.items[param_index];
    if (!param->name) continue;
    char *param_type = call_param_type_text(fun, param_index, bindings, binding_len);
    bool mutref_param = type_is_named_generic(param_type, "mutref");
    free(param_type);
    if (!mutref_param) continue;
    ValueProvenance value = {0};
    if (scope_copy_value_provenance(&scope, param->name, &value)) {
      for (size_t i = 0; i < value.len; i++) {
        if (!function_param_index_by_name(fun, value.items[i].origin.root, NULL)) summary->callee_local_storage = true;
      }
      provenance_storage_effect_vec_add(&summary->storage_effects, param->name, NULL, NULL, &value, true);
    }
    value_provenance_free(&value);
  }

  scope_free(&scope);
  checking_function = previous_checking_function;
  return_provenance_expr_bindings = previous_expr_bindings;
  return_provenance_expr_binding_len = previous_expr_binding_len;
  function_return_provenance_depth--;
  return summary->return_complete && summary->effect_complete;
}

static bool function_return_value_provenance(const Program *program, const Function *fun, GenericBinding *bindings, size_t binding_len, ValueProvenance *origins, bool *may_return) {
  if (may_return) *may_return = true;
  if (!program || !fun || !origins) return false;
  FunctionProvenanceSummary summary = {0};
  bool ok = function_provenance_summary(program, fun, bindings, binding_len, &summary);
  if (may_return) *may_return = summary.may_return;
  value_provenance_add_all(origins, &summary.return_value);
  function_provenance_summary_free(&summary);
  return ok;
}

static const Expr *call_actual_for_param(const Expr *call, const Expr *receiver, size_t param_offset, size_t param_index) {
  if (receiver && param_offset == 1 && param_index == 0) return receiver;
  if (!call || param_index < param_offset) return NULL;
  size_t arg_index = param_index - param_offset;
  return arg_index < call->args.len ? call->args.items[arg_index] : NULL;
}

static bool function_param_index_by_name(const Function *fun, const char *name, size_t *out_index) {
  if (!fun || !name) return false;
  for (size_t i = 0; i < fun->params.len; i++) {
    if (fun->params.items[i].name && strcmp(fun->params.items[i].name, name) == 0) {
      if (out_index) *out_index = i;
      return true;
    }
  }
  return false;
}

static bool collect_effect_target_places(const Program *program, const Expr *actual, Scope *scope, PlaceVec *places) {
  if (!program || !actual || !scope || !places) return false;
  bool added = false;
  ValueProvenance direct = {0};
  if (expr_reference_provenance(program, actual, scope, &direct)) {
    for (size_t i = 0; i < direct.len; i++) {
      ProvenanceEntry *entry = &direct.items[i];
      if (origin_path_text(entry->value_path)[0]) continue;
      if (place_vec_add(places, entry->origin.root, entry->origin.root_scope, entry->origin.path)) added = true;
    }
  }
  value_provenance_free(&direct);
  if (added) return true;

  const Expr *place_expr = actual && actual->kind == EXPR_BORROW ? actual->left : actual;
  char root[128];
  char path[256];
  if (expr_binding_path(place_expr, root, sizeof(root), path, sizeof(path)) && scope_has(scope, root)) {
    return place_vec_add(places, root, scope_binding_scope(scope, root), path);
  }
  return false;
}

static bool place_storage_value_provenance_under_path(const Program *program, Scope *scope, const Place *place, const char *relative_path, ValueProvenance *out) {
  if (!program || !scope || !place || !place->root || !out) return false;
  bool added = false;
  char *full_path = origin_path_join(place->path, relative_path);

  ValueProvenance existing = {0};
  if (scope_copy_value_provenance_from_scope(scope, place->root_scope, place->root, &existing)) {
    if (value_provenance_add_all_under_path(out, &existing, full_path)) added = true;
  }
  value_provenance_free(&existing);

  if (!added) {
    const char *root_type = scope_type_in_binding_scope(scope, place->root_scope, place->root);
    char storage_type[192];
    if (named_ref_inner_text(root_type, "ref", storage_type, sizeof(storage_type)) ||
        named_ref_inner_text(root_type, "mutref", storage_type, sizeof(storage_type))) {
      root_type = storage_type;
    }
    ValueProvenance typed = {0};
    if (type_value_provenance_from_place(program, root_type, scope, place->root, place->root_scope ? place->root_scope : scope_binding_scope(scope, place->root), NULL, NULL, &typed, 0)) {
      if (value_provenance_add_all_under_path(out, &typed, full_path)) added = true;
    }
    value_provenance_free(&typed);
  }

  free(full_path);
  return added;
}

static bool actual_storage_value_provenance_under_path(const Program *program, const Expr *actual, Scope *scope, const char *relative_path, ValueProvenance *out) {
  if (!program || !actual || !scope || !out) return false;
  bool added = false;
  PlaceVec places = {0};
  if (collect_effect_target_places(program, actual, scope, &places)) {
    for (size_t i = 0; i < places.len; i++) {
      if (place_storage_value_provenance_under_path(program, scope, &places.items[i], relative_path, out)) added = true;
    }
  }
  place_vec_free(&places);
  return added;
}

static bool instantiate_call_provenance_entry(const Program *program, const Function *callee, const Expr *call, const Expr *receiver, size_t param_offset, Scope *scope, GenericBinding *bindings, size_t binding_len, const ProvenanceEntry *summary_entry, ValueProvenance *out) {
  if (!program || !callee || !call || !scope || !summary_entry || !out) return false;
  size_t param_index = callee->params.len;
  if (!function_param_index_by_name(callee, summary_entry->origin.root, &param_index)) return false;
  const Expr *actual = call_actual_for_param(call, receiver, param_offset, param_index);
  if (!actual) return false;

  bool added = false;
  ValueProvenance actual_origins = {0};
  char *param_type = call_param_type_text(callee, param_index, bindings, binding_len);
  bool reference_param = type_is_named_generic(param_type, "ref") || type_is_named_generic(param_type, "mutref");
  free(param_type);
  if (reference_param && callee->params.items[param_index].name &&
      strcmp(callee->params.items[param_index].name, summary_entry->origin.root) == 0) {
    ValueProvenance storage_origins = {0};
    if (actual_storage_value_provenance_under_path(program, actual, scope, summary_entry->origin.path, &storage_origins)) {
      if (value_provenance_add_all_as_with_prefix(out, &storage_origins, summary_entry->mutable_borrow, summary_entry->value_path)) added = true;
      value_provenance_free(&storage_origins);
      return added;
    }
    value_provenance_free(&storage_origins);
  }
  const char *actual_type = expr_type(program, actual, scope);
  bool actual_ref_like = type_is_named_generic(actual_type, "ref") || type_is_named_generic(actual_type, "mutref");
  char actual_root[128];
  char actual_path[256];
  bool implicit_reference_actual = reference_param && !actual_ref_like &&
    expr_binding_path(actual, actual_root, sizeof(actual_root), actual_path, sizeof(actual_path)) &&
    scope_has(scope, actual_root);
  if (expr_reference_provenance_as(program, actual, scope, &actual_origins, summary_entry->mutable_borrow)) {
    ValueProvenance selected_origins = {0};
    ValueProvenance *source_origins = &actual_origins;
    if (summary_entry->origin.path) {
      if (value_provenance_add_all_under_path(&selected_origins, &actual_origins, summary_entry->origin.path)) {
        source_origins = &selected_origins;
      } else if (reference_param) {
        if (implicit_reference_actual) {
          if (value_provenance_add_actual_place(out, actual, scope, summary_entry)) added = true;
        } else if (value_provenance_add_all_as_with_origin_suffix(out, &actual_origins, summary_entry->mutable_borrow, summary_entry->value_path, summary_entry->origin.path)) {
          added = true;
        }
        source_origins = NULL;
      } else {
        source_origins = NULL;
      }
    } else if (implicit_reference_actual) {
      if (value_provenance_add_actual_place(out, actual, scope, summary_entry)) added = true;
      source_origins = NULL;
    }
    if (source_origins && value_provenance_add_all_as_with_prefix(out, source_origins, summary_entry->mutable_borrow, summary_entry->value_path)) added = true;
    value_provenance_free(&selected_origins);
  } else {
    char root[128];
    char path[256];
    if (expr_binding_path(actual, root, sizeof(root), path, sizeof(path)) && scope_has(scope, root)) {
      if (reference_param) {
        if (value_provenance_add_actual_place(out, actual, scope, summary_entry)) added = true;
      } else {
        char *origin_path = origin_path_join(path, summary_entry->origin.path);
        bool local_storage = summary_entry->origin.path ? reference_place_origin_is_local_storage(scope, root) : reference_source_origin_is_local_storage(scope, root);
        if (value_provenance_add_full(out, root, scope_binding_scope(scope, root), summary_entry->mutable_borrow, local_storage, summary_entry->value_path, origin_path)) added = true;
        free(origin_path);
      }
    }
  }
  value_provenance_free(&actual_origins);
  return added;
}

static bool validate_installed_provenance_lifetimes(Scope *scope, const char *target_root, Scope *target_root_scope, const ValueProvenance *origins, const Expr *site, ZDiag *diag) {
  if (!scope || !target_root || !origins) return true;
  Scope *target_scope = target_root_scope ? target_root_scope : scope_binding_scope(scope, target_root);
  for (size_t i = 0; i < origins->len; i++) {
    const ProvenanceEntry *entry = &origins->items[i];
    Scope *root_scope = entry->origin.root_scope ? entry->origin.root_scope : scope_binding_scope(scope, entry->origin.root);
    if (target_scope && root_scope && !scope_is_ancestor_or_self(root_scope, target_scope)) {
      char actual[256];
      snprintf(actual, sizeof(actual), "reference to shorter-lived local '%s'", entry->origin.root);
      return set_diag_detail(diag, 3030, "cannot store a reference to a shorter-lived binding through receiver method call", site ? site->line : 0, site ? site->column : 0, "borrow source that outlives the receiver storage", actual, "keep the receiver in the same lexical scope as the borrowed value");
    }
  }
  return true;
}

static bool function_storage_effect_summary(const Program *program, const Function *fun, GenericBinding *bindings, size_t binding_len, ProvenanceStorageEffectVec *effects) {
  if (!program || !fun || !effects) return false;
  FunctionProvenanceSummary summary = {0};
  bool ok = function_provenance_summary(program, fun, bindings, binding_len, &summary);
  for (size_t i = 0; i < summary.storage_effects.len; i++) {
    ProvenanceStorageEffect *effect = &summary.storage_effects.items[i];
    provenance_storage_effect_vec_add(effects, effect->target.root, effect->target.root_scope, effect->target.path, &effect->value, effect->overwrite);
  }
  function_provenance_summary_free(&summary);
  return ok;
}

static bool apply_provenance_storage_effect(const Program *program, const ResolvedProvenanceCall *resolved, const ProvenanceStorageEffect *effect, Scope *scope, ZDiag *diag) {
  if (!program || !resolved || !resolved->callee || !resolved->call || !effect || !scope) return true;
  size_t param_index = resolved->callee->params.len;
  if (!function_param_index_by_name(resolved->callee, effect->target.root, &param_index)) return true;
  const Expr *actual = call_actual_for_param(resolved->call, resolved->receiver, resolved->param_offset, param_index);
  if (!actual) return true;

  PlaceVec targets = {0};
  if (!collect_effect_target_places(program, actual, scope, &targets)) {
    place_vec_free(&targets);
    return true;
  }

  ValueProvenance instantiated = {0};
  for (size_t i = 0; i < effect->value.len; i++) {
    const ProvenanceEntry *entry = &effect->value.items[i];
    if (!function_param_index_by_name(resolved->callee, entry->origin.root, NULL)) {
      char actual_detail[256];
      snprintf(actual_detail, sizeof(actual_detail), "reference to callee-local '%s'", entry->origin.root ? entry->origin.root : "<unknown>");
      value_provenance_free(&instantiated);
      place_vec_free(&targets);
      return set_diag_detail(diag, 3030, "cannot store a reference to a callee-local binding through a mutable parameter", resolved->call->line, resolved->call->column, "borrow source that outlives the call", actual_detail, "store only references derived from caller-owned arguments into mutable parameter storage");
    }
    instantiate_call_provenance_entry(program, resolved->callee, resolved->call, resolved->receiver, resolved->param_offset, scope, resolved->bindings, resolved->binding_len, entry, &instantiated);
  }
  if (instantiated.len == 0) {
    value_provenance_free(&instantiated);
    place_vec_free(&targets);
    return true;
  }

  for (size_t i = 0; i < targets.len; i++) {
    Place *target = &targets.items[i];
    char *target_path = origin_path_join(target->path, effect->target.path);
    ValueProvenance target_effects = {0};
    if (!effect->overwrite || targets.len > 1) {
      place_storage_value_provenance_under_path(program, scope, target, effect->target.path, &target_effects);
    }
    value_provenance_add_all(&target_effects, &instantiated);
    if (!validate_installed_provenance_lifetimes(scope, target->root, target->root_scope, &target_effects, resolved->call, diag)) {
      free(target_path);
      value_provenance_free(&target_effects);
      value_provenance_free(&instantiated);
      place_vec_free(&targets);
      return false;
    }
    value_provenance_free(&target_effects);
    free(target_path);
  }

  for (size_t i = 0; i < targets.len; i++) {
    Place *target = &targets.items[i];
    char *target_path = origin_path_join(target->path, effect->target.path);
    ValueProvenance target_effects = {0};
    if (!effect->overwrite || targets.len > 1) {
      place_storage_value_provenance_under_path(program, scope, target, effect->target.path, &target_effects);
    }
    value_provenance_add_all(&target_effects, &instantiated);
    scope_set_value_provenance_path_in_scope(scope, target->root_scope, target->root, target_path, &target_effects);
    value_provenance_free(&target_effects);
    free(target_path);
  }

  value_provenance_free(&instantiated);
  place_vec_free(&targets);
  return true;
}

static bool apply_provenance_call_storage_effects(const Program *program, const ResolvedProvenanceCall *resolved, Scope *scope, ZDiag *diag) {
  if (!program || !resolved || !resolved->callee || !resolved->call || !scope) return true;
  ProvenanceStorageEffectVec effects = {0};
  bool complete = function_storage_effect_summary(program, resolved->callee, resolved->bindings, resolved->binding_len, &effects);
  if (!complete) {
    for (size_t i = 0; i < resolved->callee->params.len; i++) {
      char *param_type = call_param_type_text(resolved->callee, i, resolved->bindings, resolved->binding_len);
      bool mutref_param = type_is_named_generic(param_type, "mutref");
      free(param_type);
      if (!mutref_param) continue;
      provenance_storage_effect_vec_free(&effects);
      return set_diag_detail(diag, 3030, "cannot verify provenance effects for mutable parameter call", resolved->call->line, resolved->call->column, "complete provenance summary", "recursive or incomplete mutable parameter summary", "simplify the call cycle or keep reference-storing mutable calls non-recursive");
    }
  }
  bool ok = true;
  for (size_t i = 0; ok && i < effects.len; i++) {
    ok = apply_provenance_storage_effect(program, resolved, &effects.items[i], scope, diag);
  }
  provenance_storage_effect_vec_free(&effects);
  return ok;
}

static bool apply_checked_call_storage_effects(const Program *program, const Expr *expr, Scope *scope, ZDiag *diag) {
  if (!program || !expr || expr->kind != EXPR_CALL || !expr->left || !scope) return true;
  ResolvedProvenanceCall resolved = {0};
  if (!resolve_provenance_call(program, expr, scope, expr_type(program, expr, scope), checking_function, return_provenance_expr_bindings, return_provenance_expr_binding_len, &resolved)) return true;
  bool ok = apply_provenance_call_storage_effects(program, &resolved, scope, diag);
  resolved_provenance_call_free(&resolved);
  return ok;
}

static bool apply_resolved_call_storage_effects(const Program *program, const Expr *expr, Scope *scope, ZDiag *diag) {
  return apply_checked_call_storage_effects(program, expr, scope, diag);
}

static bool apply_expr_call_storage_effects(const Program *program, const Expr *expr, Scope *scope, ZDiag *diag) {
  if (!program || !expr || !scope) return true;
  if (expr->kind == EXPR_RESCUE) {
    if (!apply_expr_call_storage_effects(program, expr->left, scope, diag)) return false;
    ProvenanceScopeSnapshot *before_right = provenance_scope_snapshot_capture(scope);
    if (!apply_expr_call_storage_effects(program, expr->right, scope, diag)) {
      provenance_scope_snapshot_restore(before_right);
      provenance_scope_snapshot_free(before_right);
      return false;
    }
    ProvenanceScopeSnapshot *right_after = provenance_scope_snapshot_capture(scope);
    provenance_scope_snapshot_restore_optional_branch(before_right, right_after, true, true);
    provenance_scope_snapshot_free(right_after);
    provenance_scope_snapshot_free(before_right);
    return true;
  }
  if (expr->kind == EXPR_BINARY && expr->text && (strcmp(expr->text, "&&") == 0 || strcmp(expr->text, "||") == 0)) {
    if (!apply_expr_call_storage_effects(program, expr->left, scope, diag)) return false;
    ProvenanceScopeSnapshot *before_right = provenance_scope_snapshot_capture(scope);
    if (!apply_expr_call_storage_effects(program, expr->right, scope, diag)) {
      provenance_scope_snapshot_restore(before_right);
      provenance_scope_snapshot_free(before_right);
      return false;
    }
    ProvenanceScopeSnapshot *right_after = provenance_scope_snapshot_capture(scope);
    bool left_is_bool_literal = expr->left && expr->left->kind == EXPR_BOOL;
    bool left_value = left_is_bool_literal && expr->left->bool_value;
    bool include_before = true;
    bool include_after = true;
    if (left_is_bool_literal && strcmp(expr->text, "&&") == 0) {
      include_before = !left_value;
      include_after = left_value;
    } else if (left_is_bool_literal && strcmp(expr->text, "||") == 0) {
      include_before = left_value;
      include_after = !left_value;
    }
    provenance_scope_snapshot_restore_optional_branch(before_right, right_after, include_before, include_after);
    provenance_scope_snapshot_free(right_after);
    provenance_scope_snapshot_free(before_right);
    return true;
  }
  if (!apply_expr_call_storage_effects(program, expr->left, scope, diag) ||
      !apply_expr_call_storage_effects(program, expr->right, scope, diag)) return false;
  for (size_t i = 0; i < expr->args.len; i++) {
    if (!apply_expr_call_storage_effects(program, expr->args.items[i], scope, diag)) return false;
  }
  for (size_t i = 0; i < expr->fields.len; i++) {
    if (!apply_expr_call_storage_effects(program, expr->fields.items[i].value, scope, diag)) return false;
  }
  if (expr->kind == EXPR_CALL) return apply_resolved_call_storage_effects(program, expr, scope, diag);
  return true;
}

static bool check_assignment_not_borrowed(const Expr *target, Scope *scope, ZDiag *diag) {
  char root[128];
  char path[256];
  if (!expr_binding_path(target, root, sizeof(root), path, sizeof(path))) return true;
  size_t shared = 0;
  size_t mut = 0;
  scope_borrow_counts_for_place(scope, root, path, &shared, &mut);
  if (shared == 0 && mut == 0) return true;
  char place[200];
  format_origin_place(place, sizeof(place), root, path);
  char actual[256];
  snprintf(actual, sizeof(actual), "%.160s has %zu shared and %zu mutable borrow(s)", place, shared, mut);
  set_diag_detail(diag, 3029, "cannot assign to a value while it is borrowed", target->line, target->column, "unborrowed assignment target", actual, "end the borrow's lexical scope before assigning to this value");
  ZBorrowTrace active[Z_BORROW_TRACE_MAX] = {0};
  size_t active_len = 0;
  bool truncated = false;
  if (scope_active_borrows_for_place(scope, root, path, false, active, &active_len, &truncated)) {
    set_diag_borrow_trace(diag, active, active_len, truncated, "move the assignment after the active borrow's lexical scope or put the borrow in an inner block");
  }
  return false;
}

static bool check_return_borrow_escape(const Program *program, const Expr *expr, Scope *scope, ZDiag *diag) {
  if (!expr) return true;
  ValueProvenance origins = {0};
  if (!expr_reference_provenance(program, expr, scope, &origins)) {
    value_provenance_free(&origins);
    return true;
  }
  for (size_t i = 0; i < origins.len; i++) {
    ProvenanceEntry *entry = &origins.items[i];
    if (entry->local_storage) {
      char actual[256];
      snprintf(actual, sizeof(actual), "reference to local '%s'", entry->origin.root);
      value_provenance_free(&origins);
      return set_diag_detail(diag, 3030, "cannot return a reference to a local binding", expr->line, expr->column, "reference derived from a parameter or longer-lived value", actual, "return an owned value or keep the borrow inside the current function");
    }
  }
  value_provenance_free(&origins);
  return true;
}

static bool check_return_call_borrow_escape(const Program *program, const Expr *expr, Scope *scope, ZDiag *diag) {
  if (!expr || expr->kind != EXPR_CALL || !expr->left) return true;
  ValueProvenance origins = {0};
  if (!call_result_value_provenance(program, expr, scope, &origins)) {
    value_provenance_free(&origins);
    return true;
  }
  for (size_t i = 0; i < origins.len; i++) {
    ProvenanceEntry *entry = &origins.items[i];
    if (entry->local_storage) {
      char actual[256];
      snprintf(actual, sizeof(actual), "call may return reference derived from local '%s'", entry->origin.root);
      value_provenance_free(&origins);
      return set_diag_detail(diag, 3030, "cannot return a reference derived from a local call argument", expr->line, expr->column, "reference derived from a parameter or longer-lived value", actual, "keep local borrows inside the current function or return an owned value");
    }
  }
  value_provenance_free(&origins);
  return true;
}

static bool check_return_reference_escape(const Program *program, const Expr *expr, Scope *scope, ZDiag *diag) {
  if (!check_return_call_borrow_escape(program, expr, scope, diag)) return false;
  return check_return_borrow_escape(program, expr, scope, diag);
}

static bool param_vec_contains_name(const ParamVec *params, const char *name) {
  if (!params || !name) return false;
  for (size_t i = 0; i < params->len; i++) {
    if (params->items[i].name && strcmp(params->items[i].name, name) == 0) return true;
  }
  return false;
}

static void collect_visible_type_names(Scope *scope, ParamVec *out) {
  if (!out) return;
  for (Scope *cursor = scope; cursor; cursor = cursor->parent) {
    for (size_t i = 0; i < cursor->len; i++) {
      if (!cursor->names[i] || !cursor->types[i] || strcmp(cursor->types[i], "Type") != 0) continue;
      if (param_vec_contains_name(out, cursor->names[i])) continue;
      out->items = checker_grow_items(out->items, out->len, &out->cap, 8, sizeof(Param));
      out->items[out->len++] = (Param){
        .name = cursor->names[i],
        .type = "Type",
      };
    }
  }
}

static bool validate_local_type_names(const Program *program, const Function *fun, Scope *scope, const char *type, ZDiag *diag, int line, int column) {
  if (!type) return true;
  ParamVec visible = {0};
  collect_visible_type_names(scope, &visible);
  bool ok = validate_type_names(program, type, &visible, fun ? &fun->type_params : NULL, false, diag, line, column);
  free(visible.items);
  return ok;
}

static bool parse_match_int_literal(const char *text, unsigned long long *out) {
  if (!text || !*text) return false;
  char digits[64];
  size_t len = 0;
  for (size_t i = 0; text[i] && len + 1 < sizeof(digits); i++) {
    if (text[i] == '_') continue;
    if (text[i] < '0' || text[i] > '9') break;
    digits[len++] = text[i];
  }
  digits[len] = '\0';
  if (len == 0) return false;
  char *end = NULL;
  unsigned long long value = strtoull(digits, &end, 10);
  if (!end || *end != '\0') return false;
  *out = value;
  return true;
}

static bool check_match_guard(const Program *program, MatchArm *arm, Scope *scope, ZDiag *diag) {
  if (!arm->guard) return true;
  if (!check_expr(program, arm->guard, scope, diag)) return false;
  const char *guard_type = expr_type(program, arm->guard, scope);
  if (!is_bool_type(guard_type)) return set_diag_detail(diag, 3111, "match arm guard must be Bool", arm->guard->line, arm->guard->column, "Bool", guard_type, "use a boolean condition after `if`");
  return true;
}

static bool check_scalar_match(const Program *program, const Function *fun, const Stmt *stmt, Scope *scope, ZDiag *diag, int loop_depth, const char *match_type) {
  bool is_bool = is_bool_type(match_type);
  bool is_u8 = strcmp(match_type, "u8") == 0;
  bool bool_seen[2] = {false, false};
  bool u8_seen[256] = {false};
  bool has_fallback = false;
  for (size_t arm_index = 0; arm_index < stmt->match_arms.len; arm_index++) {
    MatchArm *arm = &stmt->match_arms.items[arm_index];
    if (arm->payload_name) return set_diag_detail(diag, 3110, "scalar match arm cannot bind a payload", arm->line, arm->column, "case { ... }", "payload binding on scalar case", "remove the payload binding");
    if (strcmp(arm->case_name, "_") == 0) {
      if (arm->range_end) return set_diag_detail(diag, 3111, "fallback match arm cannot be a range", arm->line, arm->column, "_", "range fallback", "use either a fallback or an integer range");
      if (arm->guard) return set_diag_detail(diag, 3111, "fallback match arm cannot have a guard", arm->line, arm->column, "unguarded _ fallback", "guarded fallback", "move the condition to a concrete arm before the fallback");
      if (has_fallback) return set_diag_detail(diag, 3107, "duplicate match fallback arm", arm->line, arm->column, "one fallback arm", "duplicate fallback", "remove the duplicate fallback arm");
      has_fallback = true;
    } else if (is_bool) {
      if (arm->range_end || (strcmp(arm->case_name, "true") != 0 && strcmp(arm->case_name, "false") != 0)) return set_diag_detail(diag, 3103, "Bool match arm must be true, false, or _", arm->line, arm->column, "true, false, or _", arm->case_name, "match each boolean value or add a fallback");
      int index = strcmp(arm->case_name, "true") == 0 ? 1 : 0;
      if (!arm->guard && bool_seen[index]) return set_diag_detail(diag, 3107, "duplicate match arm", arm->line, arm->column, "one unguarded arm per scalar value", arm->case_name, "remove the duplicate arm");
      if (!arm->guard) bool_seen[index] = true;
    } else {
      unsigned long long start = 0;
      unsigned long long end = 0;
      if (!parse_match_int_literal(arm->case_name, &start)) return set_diag_detail(diag, 3103, "integer match arm must be an integer literal, range, or _", arm->line, arm->column, "integer literal, range, or _", arm->case_name, "use a numeric case such as `0` or `0..3`");
      end = start;
      if (arm->range_end && !parse_match_int_literal(arm->range_end, &end)) return set_diag_detail(diag, 3103, "integer range end must be an integer literal", arm->line, arm->column, "integer literal range end", arm->range_end, "use a numeric range end");
      if (end < start) return set_diag_detail(diag, 3108, "integer match range must not be empty", arm->line, arm->column, "start <= end", arm->case_name, "reverse the range bounds");
      if (is_u8) {
        if (end > 255) return set_diag_detail(diag, 3108, "u8 match range is out of bounds", arm->line, arm->column, "0..255", arm->range_end ? arm->range_end : arm->case_name, "keep u8 ranges within 0..255");
        if (!arm->guard) {
          for (unsigned long long value = start; value <= end; value++) {
            if (u8_seen[value]) return set_diag_detail(diag, 3107, "duplicate match arm", arm->line, arm->column, "non-overlapping integer cases", arm->case_name, "remove the overlapping case or range");
            u8_seen[value] = true;
          }
        }
      }
    }
    if (!check_match_guard(program, arm, scope, diag)) return false;
  }
  if (!has_fallback) {
    if (is_bool) {
      if (!bool_seen[0] || !bool_seen[1]) return set_diag_detail(diag, 3106, "non-exhaustive Bool match", stmt->line, stmt->column, "true and false arms", !bool_seen[0] ? "false" : "true", "add the missing boolean arm or a fallback `_` arm");
    } else if (is_u8) {
      for (size_t value = 0; value < 256; value++) {
        if (!u8_seen[value]) return set_diag_detail(diag, 3106, "non-exhaustive u8 match", stmt->line, stmt->column, "0..255 coverage or fallback", "missing integer value", "add a covering range or a fallback `_` arm");
      }
    } else {
      return set_diag_detail(diag, 3106, "non-exhaustive integer match", stmt->line, stmt->column, "fallback `_` arm", "open integer domain", "add a fallback `_` arm for integer matches outside finite u8 coverage");
    }
  }
  ProvenanceScopeSnapshot *before = provenance_scope_snapshot_capture(scope);
  ProvenanceScopeSnapshot **arm_states = z_checked_calloc(stmt->match_arms.len, sizeof(ProvenanceScopeSnapshot *));
  bool *arm_continues = z_checked_calloc(stmt->match_arms.len, sizeof(bool));
  for (size_t arm_index = 0; arm_index < stmt->match_arms.len; arm_index++) {
    provenance_scope_snapshot_restore(before);
    Scope arm_scope = {.parent = scope};
    bool ok = check_stmt_vec_with_loop(program, fun, &stmt->match_arms.items[arm_index].body, &arm_scope, diag, loop_depth);
    scope_free(&arm_scope);
    if (!ok) {
      provenance_scope_snapshot_restore(before);
      for (size_t i = 0; i < stmt->match_arms.len; i++) provenance_scope_snapshot_free(arm_states[i]);
      free(arm_states);
      free(arm_continues);
      provenance_scope_snapshot_free(before);
      return false;
    }
    arm_states[arm_index] = provenance_scope_snapshot_capture(scope);
    arm_continues[arm_index] = !stmt_vec_guarantees_exit(&stmt->match_arms.items[arm_index].body, fun->raises);
  }
  provenance_scope_snapshot_restore_union(before, arm_states, arm_continues, stmt->match_arms.len);
  for (size_t i = 0; i < stmt->match_arms.len; i++) provenance_scope_snapshot_free(arm_states[i]);
  free(arm_states);
  free(arm_continues);
  provenance_scope_snapshot_free(before);
  return true;
}

static bool check_stmt(const Program *program, const Function *fun, const Stmt *stmt, Scope *scope, ZDiag *diag, int loop_depth) {
  if (stmt->kind == STMT_LET) {
    for (size_t i = 0; i < scope->len; i++) {
      if (strcmp(scope->names[i], stmt->name) == 0) {
        return set_diag_detail(diag, 3002, "duplicate local binding", stmt->line, stmt->column, "fresh local name", "name is already visible", "rename this binding");
      }
    }
    if (!validate_type_form(stmt->type, diag, stmt->line, stmt->column)) return false;
    if (!validate_local_type_names(program, fun, scope, stmt->type, diag, stmt->line, stmt->column)) return false;
    if (!check_expr_expected(program, stmt->expr, scope, diag, stmt->type)) return false;
    const char *actual = expr_type(program, stmt->expr, scope);
    if (stmt->type && !types_compatible(stmt->type, actual)) {
      return set_diag_detail(diag, 3006, "let binding type does not match initializer", stmt->line, stmt->column, stmt->type, actual, "change the annotation or initializer");
    }
    const char *binding_type = stmt->type ? stmt->type : actual;
    mark_owned_move_if_needed(stmt->expr, scope, binding_type);
    set_stmt_resolved_type(stmt, binding_type);
    scope_add_decl(scope, stmt->name, binding_type, stmt->mutable_binding, stmt->line, stmt->column);
    register_borrow_binding(program, stmt, scope);
    return true;
  }
  if (stmt->kind == STMT_ASSIGN) {
    const Expr *target = stmt->target;
    char expected[128];
    if (!check_lvalue_target(program, target, scope, diag, expected, sizeof(expected))) return false;
    if (!check_assignment_not_borrowed(target, scope, diag)) return false;
    AssignmentProvenanceSnapshot provenance_snapshot = {0};
    assignment_provenance_snapshot_clear(target, scope, &provenance_snapshot);
    if (!check_expr_expected(program, stmt->expr, scope, diag, expected)) {
      assignment_provenance_snapshot_restore(scope, &provenance_snapshot);
      return false;
    }
    assignment_provenance_snapshot_restore(scope, &provenance_snapshot);
    const char *actual = expr_type(program, stmt->expr, scope);
    if (!types_compatible(expected, actual)) {
      return set_diag_detail(diag, 3006, "assignment type does not match binding", stmt->line, stmt->column, expected, actual, "assign a compatible value");
    }
    mark_owned_move_if_needed(stmt->expr, scope, expected);
    mark_owned_target_live_if_needed(target, scope, expected);
    if (!update_borrow_assignment(program, target, stmt->expr, scope, diag)) return false;
    return true;
  }
  if (stmt->kind == STMT_CHECK) {
    if (!fun->raises) return set_diag_detail(diag, 1001, "`check` requires function to be marked raises", stmt->line, stmt->column, "function signature marked raises", "function is not marked raises", "add raises to the function signature");
    allow_fallible_call++;
    bool checked_ok = check_expr(program, stmt->expr, scope, diag);
    allow_fallible_call--;
    if (!checked_ok) return false;
    const Function *callee = fallible_callee(program, stmt->expr);
    if (callee && !function_error_sets_compatible(fun, callee, diag, stmt->expr)) return false;
    const char *builtin_type = builtin_fallible_return_type(stmt->expr);
    if (builtin_type && !function_error_sets_include_builtin(fun, diag, stmt->expr)) return false;
    const char *checked_type = expr_type(program, stmt->expr, scope);
    bool world_stream_write = is_world_stream_write_call(stmt->expr, scope);
    const char *inner = NULL;
    size_t inner_len = 0;
    bool maybe_value = type_has_generic_arg(checked_type, "Maybe", &inner, &inner_len);
    if (!callee && !builtin_type && !world_stream_write && !maybe_value) {
      return set_diag_detail(diag, 1001, "`check` expects Maybe<T> or a fallible function call", stmt->line, stmt->column, "Maybe<T> or raises { ... } call", checked_type, "check a Maybe value or a named-error fallible function");
    }
    if (type_is_named_generic(checked_type, "Maybe") && !type_is_named_generic(fun->return_type, "Maybe")) {
      return set_diag_detail(diag, 1001, "`check` on Maybe<T> requires a Maybe return type", stmt->line, stmt->column, "function returning Maybe<T>", fun->return_type ? fun->return_type : "Void", "return Maybe<T> from this function or handle the Maybe value explicitly");
    }
    return true;
  }
  if (stmt->kind == STMT_RAISE) {
    if (!fun->raises) return set_diag_detail(diag, 1001, "`raise` requires function to be marked raises", stmt->line, stmt->column, "function signature marked raises", "function is not marked raises", "add raises to the function signature or return an explicit value");
    if (!stmt->name) return set_diag_detail(diag, 1001, "raise requires an error name", stmt->line, stmt->column, "raise ErrorName", "missing error name", "name the error being raised");
    if (fun->has_error_set && !function_error_contains(fun, stmt->name)) {
      char actual[160];
      snprintf(actual, sizeof(actual), "raise %s", stmt->name);
      return set_diag_detail(diag, 1002, "raised error is not declared by this function", stmt->line, stmt->column, "error listed in raises { ... }", actual, "add the error name to this function's raises set");
    }
    return true;
  }
  if (stmt->kind == STMT_DEFER) return check_expr(program, stmt->expr, scope, diag);
  if (stmt->kind == STMT_RETURN) {
    if (!check_expr_expected(program, stmt->expr, scope, diag, fun->return_type)) return false;
    const char *actual = stmt->expr ? expr_type(program, stmt->expr, scope) : "Void";
    if (!types_compatible(fun->return_type, actual)) {
      return set_diag_detail(diag, 3007, "return type does not match function return type", stmt->line, stmt->column, fun->return_type, actual, "return a value compatible with the function signature");
    }
    if (!check_return_reference_escape(program, stmt->expr, scope, diag)) return false;
    mark_owned_move_if_needed(stmt->expr, scope, fun->return_type);
    return true;
  }
  if (stmt->kind == STMT_EXPR) return check_expr(program, stmt->expr, scope, diag);
  if (stmt->kind == STMT_IF) {
    if (!check_expr_expected(program, stmt->expr, scope, diag, "Bool")) return false;
    const char *condition_type = expr_type(program, stmt->expr, scope);
    if (!is_bool_type(condition_type)) return set_diag_detail(diag, 3016, "condition must be Bool", stmt->expr->line, stmt->expr->column, "Bool", condition_type, "compare explicitly or produce a Bool value");
    bool then_possible = true;
    bool else_possible = true;
    if (stmt->expr && stmt->expr->kind == EXPR_BOOL) {
      then_possible = stmt->expr->bool_value;
      else_possible = !stmt->expr->bool_value;
    }
    ProvenanceScopeSnapshot *before = provenance_scope_snapshot_capture(scope);
    Scope then_scope = {.parent = scope};
    bool ok = check_stmt_vec_with_loop(program, fun, &stmt->then_body, &then_scope, diag, loop_depth);
    scope_free(&then_scope);
    if (!ok) {
      provenance_scope_snapshot_restore(before);
      provenance_scope_snapshot_free(before);
      return false;
    }
    ProvenanceScopeSnapshot *then_after = provenance_scope_snapshot_capture(scope);
    provenance_scope_snapshot_restore(before);
    Scope else_scope = {.parent = scope};
    ok = check_stmt_vec_with_loop(program, fun, &stmt->else_body, &else_scope, diag, loop_depth);
    scope_free(&else_scope);
    if (!ok) {
      provenance_scope_snapshot_restore(before);
      provenance_scope_snapshot_free(then_after);
      provenance_scope_snapshot_free(before);
      return false;
    }
    ProvenanceScopeSnapshot *else_after = provenance_scope_snapshot_capture(scope);
    ProvenanceScopeSnapshot *states[] = {then_after, else_after};
    bool continues[] = {
      then_possible && !stmt_vec_guarantees_exit(&stmt->then_body, fun->raises),
      else_possible && !stmt_vec_guarantees_exit(&stmt->else_body, fun->raises),
    };
    provenance_scope_snapshot_restore_union(before, states, continues, 2);
    provenance_scope_snapshot_free(then_after);
    provenance_scope_snapshot_free(else_after);
    provenance_scope_snapshot_free(before);
    return true;
  }
  if (stmt->kind == STMT_WHILE) {
    if (!check_expr_expected(program, stmt->expr, scope, diag, "Bool")) return false;
    const char *condition_type = expr_type(program, stmt->expr, scope);
    if (!is_bool_type(condition_type)) return set_diag_detail(diag, 3016, "condition must be Bool", stmt->expr->line, stmt->expr->column, "Bool", condition_type, "compare explicitly or produce a Bool value");
    ProvenanceScopeSnapshot *before = provenance_scope_snapshot_capture(scope);
    Scope body_scope = {.parent = scope};
    bool ok = check_stmt_vec_with_loop(program, fun, &stmt->then_body, &body_scope, diag, loop_depth + 1);
    scope_free(&body_scope);
    if (!ok) {
      provenance_scope_snapshot_restore(before);
      provenance_scope_snapshot_free(before);
      return false;
    }
    ProvenanceScopeSnapshot *body_after = provenance_scope_snapshot_capture(scope);
    bool body_possible = !(stmt->expr && stmt->expr->kind == EXPR_BOOL && !stmt->expr->bool_value);
    ProvenanceScopeSnapshot *states[] = {before, body_after};
    bool continues[] = {true, body_possible && !stmt_vec_guarantees_exit(&stmt->then_body, fun->raises)};
    provenance_scope_snapshot_restore_union(before, states, continues, 2);
    provenance_scope_snapshot_free(body_after);
    provenance_scope_snapshot_free(before);
    return true;
  }
  if (stmt->kind == STMT_FOR) {
    if (!check_expr(program, stmt->expr, scope, diag)) return false;
    const char *start_type = expr_type(program, stmt->expr, scope);
    if (!check_expr_expected(program, stmt->range_end, scope, diag, is_int_type(start_type) ? start_type : NULL)) return false;
    const char *end_type = expr_type(program, stmt->range_end, scope);
    if (!is_int_type(start_type) || !is_int_type(end_type)) return set_diag_detail(diag, 3020, "range loop bounds must be integers", stmt->line, stmt->column, "integer-compatible range bounds", "non-integer bound", "use integer start and end expressions");
    if (!types_compatible(start_type, end_type)) return set_diag_detail(diag, 3020, "range loop bounds must have matching integer types", stmt->line, stmt->column, start_type, end_type, "use matching integer bounds until explicit casts are supported");
    set_stmt_resolved_type(stmt, start_type);
    ProvenanceScopeSnapshot *before = provenance_scope_snapshot_capture(scope);
    Scope body_scope = {.parent = scope};
    scope_add(&body_scope, stmt->name, start_type, false);
    bool ok = check_stmt_vec_with_loop(program, fun, &stmt->then_body, &body_scope, diag, loop_depth + 1);
    scope_free(&body_scope);
    if (!ok) {
      provenance_scope_snapshot_restore(before);
      provenance_scope_snapshot_free(before);
      return false;
    }
    ProvenanceScopeSnapshot *body_after = provenance_scope_snapshot_capture(scope);
    ProvenanceScopeSnapshot *states[] = {before, body_after};
    bool continues[] = {true, !stmt_vec_guarantees_exit(&stmt->then_body, fun->raises)};
    provenance_scope_snapshot_restore_union(before, states, continues, 2);
    provenance_scope_snapshot_free(body_after);
    provenance_scope_snapshot_free(before);
    return true;
  }
  if (stmt->kind == STMT_BREAK) {
    if (loop_depth <= 0) return set_diag_detail(diag, 3018, "break is only valid inside a loop", stmt->line, stmt->column, "enclosing while or for loop", "no enclosing loop", "move this break inside a loop or use return");
    return true;
  }
  if (stmt->kind == STMT_CONTINUE) {
    if (loop_depth <= 0) return set_diag_detail(diag, 3019, "continue is only valid inside a loop", stmt->line, stmt->column, "enclosing while or for loop", "no enclosing loop", "move this continue inside a loop");
    return true;
  }
  if (stmt->kind == STMT_MATCH) {
    if (!check_expr(program, stmt->expr, scope, diag)) return false;
    const char *match_type = expr_type(program, stmt->expr, scope);
    set_stmt_resolved_type(stmt, match_type);
    const EnumDecl *item_enum = find_enum(program, match_type);
    const Choice *item_choice = find_choice(program, match_type);
    const ParamVec *cases = item_enum ? &item_enum->cases : (item_choice ? &item_choice->cases : NULL);
    if (!cases) {
      if (is_bool_type(match_type) || is_int_type(match_type)) return check_scalar_match(program, fun, stmt, scope, diag, loop_depth, match_type);
      return set_diag_detail(diag, 3105, "match expects enum, choice, Bool, or integer value", stmt->line, stmt->column, "enum, choice, Bool, or integer value", match_type, "match on a supported scalar or variant binding");
    }
    bool has_fallback = false;
    for (size_t arm_index = 0; arm_index < stmt->match_arms.len; arm_index++) {
      MatchArm *arm = &stmt->match_arms.items[arm_index];
      if (arm->range_end) return set_diag_detail(diag, 3111, "variant match arm cannot be an integer range", arm->line, arm->column, "variant case name", arm->case_name, "use ranges only when matching integer values");
      if (strcmp(arm->case_name, "_") == 0) {
        if (arm->payload_name) return set_diag_detail(diag, 3110, "fallback match arm cannot bind a payload", arm->line, arm->column, "._ { ... }", "payload binding on fallback", "remove the payload binding from the fallback arm");
        if (arm->guard) return set_diag_detail(diag, 3111, "fallback match arm cannot have a guard", arm->line, arm->column, "unguarded ._ fallback", "guarded fallback", "move the condition to a concrete arm before the fallback");
        if (has_fallback) return set_diag_detail(diag, 3107, "duplicate match fallback arm", arm->line, arm->column, "one fallback arm", "duplicate fallback", "remove the duplicate fallback arm");
        has_fallback = true;
        continue;
      }
      const Param *item_case = find_case(cases, arm->case_name);
      if (!item_case) return set_diag_detail(diag, item_enum ? 3103 : 3104, "unknown match case", arm->line, arm->column, "declared variant case", arm->case_name, "rename the match arm");
      if (arm->payload_name && (!item_choice || !item_case->type)) return set_diag_detail(diag, 3110, "match arm cannot bind a payload for this case", arm->line, arm->column, "choice case with payload", arm->case_name, "remove the payload binding or add a payload to the choice case");
      for (size_t previous = 0; previous < arm_index; previous++) {
        if (!arm->guard && !stmt->match_arms.items[previous].guard && strcmp(stmt->match_arms.items[previous].case_name, arm->case_name) == 0) return set_diag_detail(diag, 3107, "duplicate match arm", arm->line, arm->column, "one unguarded arm per variant case", arm->case_name, "remove the duplicate arm");
      }
      if (!check_match_guard(program, arm, scope, diag)) return false;
    }
    for (size_t case_index = 0; case_index < cases->len; case_index++) {
      bool seen = false;
      for (size_t arm_index = 0; arm_index < stmt->match_arms.len; arm_index++) {
        if (!stmt->match_arms.items[arm_index].guard && strcmp(cases->items[case_index].name, stmt->match_arms.items[arm_index].case_name) == 0) seen = true;
      }
      if (!seen && !has_fallback) return set_diag_detail(diag, 3106, "non-exhaustive match", stmt->line, stmt->column, "one arm for every variant case", cases->items[case_index].name, "add the missing match arm or a fallback `._` arm");
    }
    ProvenanceScopeSnapshot *before = provenance_scope_snapshot_capture(scope);
    ProvenanceScopeSnapshot **arm_states = z_checked_calloc(stmt->match_arms.len, sizeof(ProvenanceScopeSnapshot *));
    bool *arm_continues = z_checked_calloc(stmt->match_arms.len, sizeof(bool));
    for (size_t arm_index = 0; arm_index < stmt->match_arms.len; arm_index++) {
      provenance_scope_snapshot_restore(before);
      Scope arm_scope = {.parent = scope};
      MatchArm *arm = &stmt->match_arms.items[arm_index];
      if (strcmp(arm->case_name, "_") != 0 && arm->payload_name && item_choice) {
        const Param *item_case = find_case(&item_choice->cases, arm->case_name);
        if (item_case && item_case->type) {
          scope_add(&arm_scope, arm->payload_name, item_case->type, false);
          register_match_payload_binding_provenance(program, stmt->expr, scope, &arm_scope, arm->payload_name, arm->case_name);
        }
      }
      bool ok = check_stmt_vec_with_loop(program, fun, &arm->body, &arm_scope, diag, loop_depth);
      scope_free(&arm_scope);
      if (!ok) {
        provenance_scope_snapshot_restore(before);
        for (size_t i = 0; i < stmt->match_arms.len; i++) provenance_scope_snapshot_free(arm_states[i]);
        free(arm_states);
        free(arm_continues);
        provenance_scope_snapshot_free(before);
        return false;
      }
      arm_states[arm_index] = provenance_scope_snapshot_capture(scope);
      arm_continues[arm_index] = !stmt_vec_guarantees_exit(&arm->body, fun->raises);
    }
    provenance_scope_snapshot_restore_union(before, arm_states, arm_continues, stmt->match_arms.len);
    for (size_t i = 0; i < stmt->match_arms.len; i++) provenance_scope_snapshot_free(arm_states[i]);
    free(arm_states);
    free(arm_continues);
    provenance_scope_snapshot_free(before);
    return true;
  }
  return true;
}

static bool check_stmt_vec_with_loop(const Program *program, const Function *fun, const StmtVec *body, Scope *scope, ZDiag *diag, int loop_depth) {
  for (size_t i = 0; i < body->len; i++) {
    if (!check_stmt(program, fun, body->items[i], scope, diag, loop_depth)) return false;
  }
  return true;
}

static bool check_stmt_vec(const Program *program, const Function *fun, const StmtVec *body, Scope *scope, ZDiag *diag) {
  return check_stmt_vec_with_loop(program, fun, body, scope, diag, 0);
}

static bool stmt_vec_guarantees_exit(const StmtVec *body, bool function_raises);

static bool stmt_guarantees_exit(const Stmt *stmt, bool function_raises) {
  if (!stmt) return false;
  if (stmt->kind == STMT_RETURN) return true;
  if (stmt->kind == STMT_RAISE) return function_raises;
  if (stmt->kind == STMT_IF) {
    return stmt_vec_guarantees_exit(&stmt->then_body, function_raises) &&
           stmt_vec_guarantees_exit(&stmt->else_body, function_raises);
  }
  if (stmt->kind == STMT_MATCH) {
    if (stmt->match_arms.len == 0) return false;
    for (size_t i = 0; i < stmt->match_arms.len; i++) {
      if (!stmt_vec_guarantees_exit(&stmt->match_arms.items[i].body, function_raises)) return false;
    }
    return true;
  }
  return false;
}

static bool stmt_vec_guarantees_exit(const StmtVec *body, bool function_raises) {
  if (!body) return false;
  for (size_t i = 0; i < body->len; i++) {
    if (stmt_guarantees_exit(body->items[i], function_raises)) return true;
  }
  return false;
}

static bool check_function_has_required_return(const Function *fun, ZDiag *diag) {
  if (!fun || !fun->return_type || strcmp(fun->return_type, "Void") == 0) return true;
  if (stmt_vec_guarantees_exit(&fun->body, fun->raises)) return true;
  return set_diag_detail(diag, 3007, "non-void function must return a value on every path", fun->line, fun->column, fun->return_type, "function body may fall through", "add an explicit return or raise on every path");
}

static bool validate_drop_method(const Shape *shape, const Function *method, ZDiag *diag) {
  if (strcmp(method->name, "drop") != 0) return true;
  if (method->raises) {
    return set_diag_detail(diag, 3014, "drop method must not raise", method->line, method->column, "fun drop(self: mutref<Self>) -> Void", "raises drop method", "make cleanup infallible or use an explicit fallible cleanup function");
  }
  if (!method->return_type || strcmp(method->return_type, "Void") != 0) {
    return set_diag_detail(diag, 3014, "drop method must return Void", method->line, method->column, "-> Void", method->return_type ? method->return_type : "missing return type", "use the canonical drop signature");
  }
  if (method->params.len != 1) {
    return set_diag_detail(diag, 3014, "drop method must take exactly self", method->line, method->column, "drop(self: mutref<Self>)", "wrong parameter count", "use exactly one self parameter");
  }
  Param *self = &method->params.items[0];
  if (!self->name || strcmp(self->name, "self") != 0 || !self->type || strcmp(self->type, "mutref<Self>") != 0) {
    return set_diag_detail(diag, 3014, "drop method must use self: mutref<Self>", self->line, self->column, "self: mutref<Self>", self->type ? self->type : "missing self type", "use the canonical drop receiver");
  }
  (void)shape;
  return true;
}

static bool check_shape_method_body(const Program *program, const Shape *shape, const Function *method, ZDiag *diag) {
  Scope scope = {0};
  size_t binding_len = 1 + shape->type_params.len;
  GenericBinding *bindings = z_checked_calloc(binding_len, sizeof(GenericBinding));
  bindings[0].name = "Self";
  bindings[0].type = shape_open_instance_type(shape);
  for (size_t i = 0; i < shape->type_params.len; i++) {
    bindings[i + 1].name = shape->type_params.items[i].name;
    bindings[i + 1].type = z_strdup(shape->type_params.items[i].name);
    if (shape->type_params.items[i].is_static) {
      scope_add(&scope, shape->type_params.items[i].name, shape->type_params.items[i].type ? shape->type_params.items[i].type : "usize", false);
    } else {
      scope_add(&scope, shape->type_params.items[i].name, "Type", false);
    }
  }
  for (size_t i = 0; i < method->params.len; i++) {
    char *param_type = type_substitute_generic(method->params.items[i].type, bindings, binding_len);
    scope_add_param_decl(&scope, method->params.items[i].name, param_type, method->params.items[i].line, method->params.items[i].column);
    free(param_type);
  }
  for (size_t shape_index = 0; shape_index < program->shapes.len; shape_index++) scope_add(&scope, program->shapes.items[shape_index].name, "Type", false);
  for (size_t interface_index = 0; interface_index < program->interfaces.len; interface_index++) scope_add(&scope, program->interfaces.items[interface_index].name, "Type", false);
  for (size_t enum_index = 0; enum_index < program->enums.len; enum_index++) scope_add(&scope, program->enums.items[enum_index].name, "Type", false);
  for (size_t choice_index = 0; choice_index < program->choices.len; choice_index++) scope_add(&scope, program->choices.items[choice_index].name, "Type", false);
  for (size_t alias_index = 0; alias_index < program->aliases.len; alias_index++) scope_add(&scope, program->aliases.items[alias_index].name, "Type", false);
  Function checking_method = *method;
  checking_method.return_type = type_substitute_generic(method->return_type, bindings, binding_len);
  bool ok = check_stmt_vec(program, &checking_method, &method->body, &scope, diag);
  if (ok) ok = check_function_has_required_return(&checking_method, diag);
  free(checking_method.return_type);
  generic_bindings_free(bindings, binding_len);
  free(bindings);
  scope_free(&scope);
  return ok;
}

static bool validate_shape_layout(const Shape *shape, ZDiag *diag) {
  if (!shape || !shape->layout || strcmp(shape->layout, "auto") == 0) return true;
  bool is_extern = strcmp(shape->layout, "extern") == 0;
  bool is_packed = strcmp(shape->layout, "packed") == 0;
  if (!is_extern && !is_packed) return true;
  for (size_t field_index = 0; field_index < shape->fields.len; field_index++) {
    Param *field = &shape->fields.items[field_index];
    bool ok = is_extern ? is_abi_scalar_type(field->type) : is_packed_scalar_type(field->type);
    if (!ok) {
      char actual[160];
      snprintf(actual, sizeof(actual), "%s field '%s': %s", shape->layout, field->name ? field->name : "<field>", field->type ? field->type : "Unknown");
      return set_diag_detail(diag, 3031, is_extern ? "extern shape field is not ABI-safe" : "packed shape field must be scalar integer-like data", field->line, field->column, is_extern ? "primitive scalar or explicit ref/mutref field" : "integer, Bool, or char field", actual, is_extern ? "use explicit scalar fields at C ABI boundaries" : "use fixed-width integer fields for packed layout");
    }
  }
  return true;
}

static bool type_references_generic_param(const char *type, const Shape *shape) {
  if (!type || !shape) return false;
  for (size_t i = 0; i < shape->type_params.len; i++) {
    const char *name = shape->type_params.items[i].name;
    if (!name) continue;
    if (strcmp(type, name) == 0) return true;
    const char *cursor = type;
    size_t name_len = strlen(name);
    while ((cursor = strstr(cursor, name)) != NULL) {
      bool left_ok = cursor == type || !(isalnum((unsigned char)cursor[-1]) || cursor[-1] == '_');
      bool right_ok = !(isalnum((unsigned char)cursor[name_len]) || cursor[name_len] == '_');
      if (left_ok && right_ok) return true;
      cursor += name_len;
    }
  }
  return false;
}

static bool validate_generic_owned_fields(const Shape *shape, ZDiag *diag) {
  if (!shape || shape->type_params.len == 0) return true;
  for (size_t field_index = 0; field_index < shape->fields.len; field_index++) {
    Param *field = &shape->fields.items[field_index];
    if (field->type && strstr(field->type, "owned<") && type_references_generic_param(field->type, shape)) {
      return set_diag_detail(diag, 3013, "generic containers cannot own generic payloads in this compiler", field->line, field->column, "non-owned generic field or concrete owned field", field->type, "store a concrete owned type or keep ownership outside the generic container until generic drop specialization lands");
    }
  }
  return true;
}

static bool validate_export_c_function(const Function *fun, ZDiag *diag) {
  if (!fun || !fun->export_c) return true;
  if (fun->raises) return set_diag_detail(diag, 3031, "export c function must not raise", fun->line, fun->column, "non-raising C ABI function", "raises export", "return an explicit status value across C ABI boundaries");
  if (!is_c_abi_type(fun->return_type)) {
    return set_diag_detail(diag, 3031, "export c return type is not ABI-safe", fun->line, fun->column, "Void or primitive scalar return type", fun->return_type ? fun->return_type : "Unknown", "use explicit scalar C ABI return types");
  }
  for (size_t i = 0; i < fun->params.len; i++) {
    const Param *param = &fun->params.items[i];
    if (!is_c_abi_type(param->type)) {
      return set_diag_detail(diag, 3031, "export c parameter type is not ABI-safe", param->line, param->column, "primitive scalar or explicit ref/mutref parameter", param->type ? param->type : "Unknown", "use explicit scalar C ABI parameter types");
    }
  }
  return true;
}

static bool validate_function_error_set(const Function *fun, ZDiag *diag) {
  if (!fun || !fun->has_error_set) return true;
  if (!fun->raises) {
    return set_diag_detail(diag, 1001, "error set requires raises", fun->line, fun->column, "raises { Error }", "error set without raises", "add raises before the error set");
  }
  for (size_t i = 0; i < fun->errors.len; i++) {
    const Param *error = &fun->errors.items[i];
    if (!error->name || !isupper((unsigned char)error->name[0])) {
      return set_diag_detail(diag, 1002, "error names must be UpperCamelCase symbols", error->line, error->column, "ErrorName", error->name ? error->name : "<error>", "start error names with an uppercase letter");
    }
    for (size_t previous = 0; previous < i; previous++) {
      if (strcmp(fun->errors.items[previous].name, error->name) == 0) {
        return set_diag_detail(diag, 1002, "duplicate error in raises set", error->line, error->column, "unique error names", error->name, "remove the duplicate error");
      }
    }
  }
  return true;
}

static bool validate_type_param_constraints(const Program *program, const Function *fun, ZDiag *diag) {
  if (!fun) return true;
  for (size_t i = 0; i < fun->type_params.len; i++) {
    const Param *param = &fun->type_params.items[i];
    if (!param->type || strcmp(param->type, "Type") == 0) continue;
    if (param->is_static) {
      if (!is_static_value_param_type(program, param->type)) {
        return set_diag_detail(diag, 3043, "static value parameter type is not supported", param->line, param->column, "integer, Bool, or enum static parameter", param->type, "use a concrete integer, Bool, or enum type for this static parameter");
      }
      continue;
    }
    const InterfaceDecl *interface = NULL;
    char **args = NULL;
    size_t arg_len = 0;
    if (!interface_constraint_parts(program, param->type, &interface, &args, &arg_len) || !interface) {
      free_type_arg_list(args, arg_len);
      return set_diag_detail(diag, 3038, "unknown interface constraint", param->line, param->column, "declared interface constraint", param->type, "declare the interface or change the generic parameter bound to Type");
    }
    free_type_arg_list(args, arg_len);
  }
  return true;
}

static bool is_builtin_type_name(const char *name) {
  if (!name) return false;
  const char *names[] = {
    "Void", "Bool", "bool", "String", "char", "Type",
    "World", "WorldStream", "Fs", "File", "ByteBuf", "NullAlloc", "FixedBufAlloc", "PageAlloc", "GeneralAlloc",
    "Vec", "Map", "Set", "Duration", "RandSource", "ProcStatus", "Address", "Net", "Conn", "Listener",
    "HttpMethod", "HttpClient", "HttpServer", "HttpResult", "HttpError", "HttpHeaderValue", "JsonDoc", "BufferedReader", "BufferedWriter",
    "Env", "Args", "Clock", "Rand", "Proc", "Alloc",
    "Maybe", "Span", "MutSpan", "ref", "mutref", "owned",
    NULL
  };
  for (size_t i = 0; names[i]; i++) {
    if (strcmp(name, names[i]) == 0) return true;
  }
  return is_int_type(name) || is_float_type(name);
}

static bool type_param_name_known(const ParamVec *primary, const ParamVec *secondary, const char *name) {
  const ParamVec *sets[] = {primary, secondary, NULL};
  for (size_t set_index = 0; sets[set_index]; set_index++) {
    const ParamVec *params = sets[set_index];
    for (size_t i = 0; i < params->len; i++) {
      if (!params->items[i].is_static && params->items[i].name && strcmp(params->items[i].name, name) == 0) return true;
    }
  }
  return false;
}

static const char *visible_concrete_type_name_kind(const Program *program, const char *name) {
  if (!name || !name[0]) return NULL;
  if (is_builtin_type_name(name)) return "built-in type";
  if (!program) return NULL;
  if (find_shape(program, name)) return "shape";
  if (find_enum(program, name)) return "enum";
  if (find_choice(program, name)) return "choice";
  if (find_alias(program, name)) return "type alias";
  for (size_t i = 0; i < program->interfaces.len; i++) {
    if (strcmp(program->interfaces.items[i].name, name) == 0) return "interface";
  }
  return NULL;
}

static bool validate_type_param_names_do_not_shadow(const Program *program, const ParamVec *params, const ParamVec *outer_params, ZDiag *diag) {
  if (!params) return true;
  for (size_t i = 0; i < params->len; i++) {
    const Param *param = &params->items[i];
    if (param->is_static || !param->name) continue;
    if (outer_params) {
      for (size_t previous = 0; previous < outer_params->len; previous++) {
        const Param *outer = &outer_params->items[previous];
        if (outer->is_static || !outer->name) continue;
        if (strcmp(outer->name, param->name) == 0) {
          char actual[160];
          snprintf(actual, sizeof(actual), "type parameter '%s' already exists in the outer generic scope", param->name);
          return set_diag_detail(diag, 3008, "generic type parameter shadows outer type parameter", param->line, param->column, "distinct generic type parameter name", actual, "rename the inner generic parameter");
        }
      }
    }
    if (strcmp(param->name, "Self") == 0) {
      return set_diag_detail(diag, 3008, "generic type parameter shadows Self type", param->line, param->column, "generic type parameter name other than Self", "'Self' is reserved for method Self types", "rename the generic parameter");
    }
    const char *kind = visible_concrete_type_name_kind(program, param->name);
    if (kind) {
      char actual[160];
      snprintf(actual, sizeof(actual), "'%s' already names a %s", param->name, kind);
      return set_diag_detail(diag, 3008, "generic type parameter shadows concrete type name", param->line, param->column, "generic type parameter name that does not reuse a visible type", actual, "rename the generic parameter or the concrete type");
    }
  }
  return true;
}

static bool program_type_name_known(const Program *program, const ParamVec *primary, const ParamVec *secondary, const char *name, bool allow_self) {
  if (!name || !name[0]) return false;
  if (allow_self && strcmp(name, "Self") == 0) return true;
  if (is_builtin_type_name(name) || type_param_name_known(primary, secondary, name)) return true;
  if (find_shape(program, name) || find_enum(program, name) || find_choice(program, name) || find_alias(program, name)) return true;
  for (size_t i = 0; program && i < program->interfaces.len; i++) {
    if (strcmp(program->interfaces.items[i].name, name) == 0) return true;
  }
  return false;
}

static bool validate_type_names_inner(const Program *program, const char *type, const ParamVec *primary, const ParamVec *secondary, bool allow_self, ZDiag *diag, int line, int column) {
  if (!type) return true;
  type = type_strip_const(type);
  if (strcmp(type, "Self") == 0 && allow_self) return true;
  if (type[0] == '[') {
    const char *close = strchr(type, ']');
    if (!close || !close[1]) return true;
    return validate_type_names_inner(program, close + 1, primary, secondary, allow_self, diag, line, column);
  }
  const char *open = strchr(type, '<');
  if (open && open > type) {
    char *name = z_strndup(type, (size_t)(open - type));
    if (!program_type_name_known(program, primary, secondary, name, allow_self)) {
      char actual[160];
      snprintf(actual, sizeof(actual), "unknown type '%s'", name);
      free(name);
      return set_diag_detail(diag, 3003, "unknown type name", line, column, "declared type name", actual, "declare the type or correct the annotation");
    }
    char **args = NULL;
    size_t arg_len = 0;
    bool parsed = type_generic_arg_list(type, name, &args, &arg_len);
    const Shape *shape = find_shape(program, name);
    if (parsed && shape) {
      if (arg_len != shape->type_params.len) {
        char actual[160];
        snprintf(actual, sizeof(actual), "%zu type argument(s)", arg_len);
        free_type_arg_list(args, arg_len);
        free(name);
        return set_diag_detail(diag, 3032, "shape type argument count mismatch", line, column, "one argument per shape type parameter", actual, "match the shape declaration's type parameter list");
      }
      for (size_t i = 0; i < arg_len; i++) {
        if (shape->type_params.items[i].is_static) continue;
        if (!validate_type_names_inner(program, args[i], primary, secondary, allow_self, diag, line, column)) {
          free_type_arg_list(args, arg_len);
          free(name);
          return false;
        }
      }
    } else if (parsed) {
      for (size_t i = 0; i < arg_len; i++) {
        if (!validate_type_names_inner(program, args[i], primary, secondary, allow_self, diag, line, column)) {
          free_type_arg_list(args, arg_len);
          free(name);
          return false;
        }
      }
    }
    free_type_arg_list(args, arg_len);
    free(name);
    return true;
  }
  if (program_type_name_known(program, primary, secondary, type, allow_self)) return true;
  char actual[160];
  snprintf(actual, sizeof(actual), "unknown type '%s'", type);
  return set_diag_detail(diag, 3003, "unknown type name", line, column, "declared type name", actual, "declare the type or correct the annotation");
}

static bool validate_type_names(const Program *program, const char *type, const ParamVec *primary, const ParamVec *secondary, bool allow_self, ZDiag *diag, int line, int column) {
  return validate_type_names_inner(program, type, primary, secondary, allow_self, diag, line, column);
}

static bool validate_interface_decl(const Program *program, const InterfaceDecl *interface, ZDiag *diag) {
  if (!interface) return true;
  for (size_t previous = 0; previous < program->interfaces.len; previous++) {
    if (&program->interfaces.items[previous] == interface) break;
    if (strcmp(program->interfaces.items[previous].name, interface->name) == 0) {
      return set_diag_detail(diag, 3008, "duplicate interface declaration", interface->line, interface->column, "unique interface name", "interface name already declared", "rename one interface");
    }
  }
  if (!validate_type_param_names_do_not_shadow(program, &interface->type_params, NULL, diag)) return false;
  for (size_t method_index = 0; method_index < interface->methods.len; method_index++) {
    const Function *method = &interface->methods.items[method_index];
    if (method->body.len > 0) {
      return set_diag_detail(diag, 3038, "interface methods cannot have bodies", method->line, method->column, "method signature only", "method body", "move implementation to a concrete shape static method");
    }
    if (!validate_type_param_names_do_not_shadow(program, &method->type_params, &interface->type_params, diag)) return false;
    if (!validate_type_form(method->return_type, diag, method->line, method->column)) return false;
    if (!validate_type_names(program, method->return_type, &interface->type_params, &method->type_params, true, diag, method->line, method->column)) return false;
    if (!validate_function_error_set(method, diag)) return false;
    for (size_t param_index = 0; param_index < method->params.len; param_index++) {
      const Param *param = &method->params.items[param_index];
      if (!validate_type_form(param->type, diag, param->line, param->column)) return false;
      if (!validate_type_names(program, param->type, &interface->type_params, &method->type_params, true, diag, param->line, param->column)) return false;
    }
    for (size_t previous = 0; previous < method_index; previous++) {
      if (strcmp(interface->methods.items[previous].name, method->name) == 0) {
        return set_diag_detail(diag, 3039, "duplicate interface method declaration", method->line, method->column, "unique method name inside interface", method->name, "rename one required method");
      }
    }
  }
  return true;
}

static bool c_header_has_function_like_macro(const char *header) {
  const char *cursor = header ? header : "";
  while ((cursor = strstr(cursor, "#define")) != NULL) {
    cursor += strlen("#define");
    while (*cursor == ' ' || *cursor == '\t') cursor++;
    while ((*cursor >= 'A' && *cursor <= 'Z') || (*cursor >= 'a' && *cursor <= 'z') || (*cursor >= '0' && *cursor <= '9') || *cursor == '_') cursor++;
    if (*cursor == '(') return true;
  }
  return false;
}

static bool validate_c_imports(const Program *program, ZDiag *diag) {
  for (size_t i = 0; program && i < program->c_imports.len; i++) {
    const CImport *import = &program->c_imports.items[i];
    ZDiag read_diag = {0};
    char *header = z_read_file(import->header, &read_diag);
    if (!header) {
      return set_diag_detail(diag, 8001, "extern c header could not be read", import->line, import->column, "readable C header path", import->header ? import->header : "<missing>", "make the header path package-relative and target-specific");
    }
    bool unsupported = false;
    const char *reason = NULL;
    if (strstr(header, "...")) {
      unsupported = true;
      reason = "variadic function";
    } else if (c_header_has_function_like_macro(header)) {
      unsupported = true;
      reason = "function-like macro";
    } else if (strstr(header, "[]")) {
      unsupported = true;
      reason = "flexible array";
    } else if (strstr(header, ":")) {
      unsupported = true;
      reason = "bitfield";
    }
    free(header);
    if (unsupported) {
      return set_diag_detail(diag, 8002, "extern c header contains unsupported C surface", import->line, import->column, "functions, constants, structs, enums, and typedefs", reason, "wrap unsupported C behind a small C shim or remove it from this imported header");
    }
  }
  return true;
}

bool z_check_program(const Program *program, ZDiag *diag) {
  meta_cache_free();
  if (!current_check_target) current_check_target = z_find_target(z_host_target());
  current_type_program = program;
  const Function *main_fun = NULL;
  bool has_test = false;
  if (!validate_c_imports(program, diag)) return false;
  for (size_t i = 0; i < program->aliases.len; i++) {
    const TypeAlias *alias = &program->aliases.items[i];
    if (!validate_type_form(alias->target, diag, alias->line, alias->column)) return false;
    if (!validate_type_names(program, alias->target, NULL, NULL, false, diag, alias->line, alias->column)) return false;
    for (size_t previous = 0; previous < i; previous++) {
      if (strcmp(program->aliases.items[previous].name, alias->name) == 0) {
        return set_diag_detail(diag, 3036, "duplicate type alias declaration", alias->line, alias->column, "unique type alias name", "type alias already declared", "rename one alias");
      }
    }
    const char *cursor = alias->target;
    for (size_t depth = 0; depth < program->aliases.len; depth++) {
      const TypeAlias *next = find_alias(program, cursor);
      if (!next) break;
      if (strcmp(next->name, alias->name) == 0) {
        return set_diag_detail(diag, 3036, "type alias cycle is not allowed", alias->line, alias->column, "alias to a concrete existing type", alias->target ? alias->target : "Unknown", "point the alias at a non-cyclic concrete type");
      }
      cursor = next->target;
    }
  }
  for (size_t i = 0; i < program->interfaces.len; i++) {
    if (!validate_interface_decl(program, &program->interfaces.items[i], diag)) return false;
  }
  Scope const_scope = {0};
  for (size_t i = 0; i < program->consts.len; i++) {
    const ConstDecl *item = &program->consts.items[i];
    const char *declared_type = item->type;
    if (item->is_public && !declared_type) {
      scope_free(&const_scope);
      return set_diag_detail(diag, 3037, "public const requires an explicit type", item->line, item->column, "pub const name: Type = value", "public const without annotation", "add a concrete public type annotation");
    }
    if (declared_type && !validate_type_form(declared_type, diag, item->line, item->column)) {
      scope_free(&const_scope);
      return false;
    }
    if (declared_type && !validate_type_names(program, declared_type, NULL, NULL, false, diag, item->line, item->column)) {
      scope_free(&const_scope);
      return false;
    }
    if (!check_expr_expected(program, item->expr, &const_scope, diag, declared_type)) {
      scope_free(&const_scope);
      return false;
    }
    const char *actual = expr_type(program, item->expr, &const_scope);
    if (declared_type && !types_compatible(declared_type, actual)) {
      bool ok = set_diag_detail(diag, 3006, "const initializer type does not match annotation", item->line, item->column, declared_type, actual, "change the literal or update the const annotation");
      scope_free(&const_scope);
      return ok;
    }
    for (size_t previous = 0; previous < i; previous++) {
      if (strcmp(program->consts.items[previous].name, item->name) == 0) {
        bool ok = set_diag_detail(diag, 3008, "duplicate const declaration", item->line, item->column, "unique const name", "const name already declared", "rename one const");
        scope_free(&const_scope);
        return ok;
      }
    }
    scope_add(&const_scope, item->name, declared_type ? declared_type : actual, false);
  }
  scope_free(&const_scope);
  for (size_t i = 0; i < program->shapes.len; i++) {
    if (!validate_type_param_names_do_not_shadow(program, &program->shapes.items[i].type_params, NULL, diag)) return false;
    for (size_t type_param_index = 0; type_param_index < program->shapes.items[i].type_params.len; type_param_index++) {
      Param *type_param = &program->shapes.items[i].type_params.items[type_param_index];
      for (size_t previous = 0; previous < type_param_index; previous++) {
        if (strcmp(program->shapes.items[i].type_params.items[previous].name, type_param->name) == 0) {
          return set_diag_detail(diag, 3008, "duplicate shape type parameter", type_param->line, type_param->column, "unique type parameter name", type_param->name, "rename one type parameter");
        }
      }
      if (type_param->is_static && !validate_type_names(program, type_param->type, NULL, NULL, false, diag, type_param->line, type_param->column)) return false;
      if (type_param->is_static && !is_static_value_param_type(program, type_param->type)) {
        return set_diag_detail(diag, 3043, "static value parameter type is not supported", type_param->line, type_param->column, "integer, Bool, or enum static parameter", type_param->type ? type_param->type : "Unknown", "use a concrete integer, Bool, or enum type for this static parameter");
      }
    }
    for (size_t field_index = 0; field_index < program->shapes.items[i].fields.len; field_index++) {
      Param *field = &program->shapes.items[i].fields.items[field_index];
      if (!validate_type_form(field->type, diag, field->line, field->column)) return false;
      if (!validate_type_names(program, field->type, &program->shapes.items[i].type_params, NULL, false, diag, field->line, field->column)) return false;
      for (size_t previous = 0; previous < field_index; previous++) {
        if (strcmp(program->shapes.items[i].fields.items[previous].name, field->name) == 0) {
          return set_diag_detail(diag, 3008, "duplicate shape field", field->line, field->column, "unique field name", field->name, "rename or remove one field");
        }
      }
    }
    if (!validate_generic_owned_fields(&program->shapes.items[i], diag)) return false;
    if (!validate_shape_layout(&program->shapes.items[i], diag)) return false;
    for (size_t method_index = 0; method_index < program->shapes.items[i].methods.len; method_index++) {
      Function *method = &program->shapes.items[i].methods.items[method_index];
      if (!validate_type_param_names_do_not_shadow(program, &method->type_params, &program->shapes.items[i].type_params, diag)) return false;
      if (!validate_shape_method_type_form(method->return_type, diag, method->line, method->column)) return false;
      if (!validate_type_names(program, method->return_type, &program->shapes.items[i].type_params, &method->type_params, true, diag, method->line, method->column)) return false;
      for (size_t param_index = 0; param_index < method->params.len; param_index++) {
        Param *param = &method->params.items[param_index];
        if (!validate_shape_method_type_form(param->type, diag, param->line, param->column)) return false;
        if (!validate_type_names(program, param->type, &program->shapes.items[i].type_params, &method->type_params, true, diag, param->line, param->column)) return false;
      }
      if (!validate_drop_method(&program->shapes.items[i], method, diag)) return false;
      if (!check_shape_method_body(program, &program->shapes.items[i], method, diag)) return false;
    }
    for (size_t other = i + 1; other < program->shapes.len; other++) {
      if (strcmp(program->shapes.items[i].name, program->shapes.items[other].name) == 0) {
        return set_diag_detail(diag, 3008, "duplicate type declaration", program->shapes.items[other].line, program->shapes.items[other].column, "unique type name", "type name already declared", "rename one type");
      }
    }
  }
  for (size_t i = 0; i < program->enums.len; i++) {
    for (size_t case_index = 0; case_index < program->enums.items[i].cases.len; case_index++) {
      Param *item_case = &program->enums.items[i].cases.items[case_index];
      if (!validate_type_form(item_case->type, diag, item_case->line, item_case->column)) return false;
      if (!validate_type_names(program, item_case->type, NULL, NULL, false, diag, item_case->line, item_case->column)) return false;
      for (size_t previous = 0; previous < case_index; previous++) {
        if (strcmp(program->enums.items[i].cases.items[previous].name, item_case->name) == 0) {
          return set_diag_detail(diag, 3008, "duplicate enum case", item_case->line, item_case->column, "unique enum case name", item_case->name, "rename or remove one case");
        }
      }
    }
    for (size_t other = i + 1; other < program->enums.len; other++) {
      if (strcmp(program->enums.items[i].name, program->enums.items[other].name) == 0) {
        return set_diag_detail(diag, 3008, "duplicate type declaration", program->enums.items[other].line, program->enums.items[other].column, "unique type name", "type name already declared", "rename one type");
      }
    }
  }
  for (size_t i = 0; i < program->choices.len; i++) {
    for (size_t case_index = 0; case_index < program->choices.items[i].cases.len; case_index++) {
      Param *item_case = &program->choices.items[i].cases.items[case_index];
      if (!validate_type_form(item_case->type, diag, item_case->line, item_case->column)) return false;
      if (!validate_type_names(program, item_case->type, NULL, NULL, false, diag, item_case->line, item_case->column)) return false;
      for (size_t previous = 0; previous < case_index; previous++) {
        if (strcmp(program->choices.items[i].cases.items[previous].name, item_case->name) == 0) {
          return set_diag_detail(diag, 3008, "duplicate choice case", item_case->line, item_case->column, "unique choice case name", item_case->name, "rename or remove one case");
        }
      }
    }
    for (size_t other = i + 1; other < program->choices.len; other++) {
      if (strcmp(program->choices.items[i].name, program->choices.items[other].name) == 0) {
        return set_diag_detail(diag, 3008, "duplicate type declaration", program->choices.items[other].line, program->choices.items[other].column, "unique type name", "type name already declared", "rename one type");
      }
    }
  }
  for (size_t i = 0; i < program->functions.len; i++) {
    const Function *fun = &program->functions.items[i];
    for (size_t other = i + 1; other < program->functions.len; other++) {
      if (strcmp(fun->name, program->functions.items[other].name) == 0) {
        return set_diag_detail(diag, 3008, "duplicate function declaration", program->functions.items[other].line, program->functions.items[other].column, "unique function name", "function name already declared", "rename one function");
      }
    }
    if (strcmp(fun->name, "main") == 0) main_fun = fun;
    if (fun->is_test) has_test = true;
    if (!validate_type_param_names_do_not_shadow(program, &fun->type_params, NULL, diag)) return false;
    if (!validate_type_form(fun->return_type, diag, fun->line, fun->column)) return false;
    if (!validate_type_names(program, fun->return_type, &fun->type_params, NULL, false, diag, fun->line, fun->column)) return false;
    if (!validate_type_param_constraints(program, fun, diag)) return false;
    if (!validate_function_error_set(fun, diag)) return false;
    if (!validate_export_c_function(fun, diag)) return false;
  }
  if (!main_fun && !has_test) return set_diag_detail(diag, 2001, "missing main function", 1, 1, "function named main", "no main function", "add pub fun main(...) -> Void");
	  for (size_t i = 0; i < program->functions.len; i++) {
	    const Function *fun = &program->functions.items[i];
	    Scope scope = {0};
	    for (size_t const_index = 0; const_index < program->consts.len; const_index++) {
	      const ConstDecl *item = &program->consts.items[const_index];
	      scope_add(&scope, item->name, item->type ? item->type : expr_type(program, item->expr, &scope), false);
	    }
	    for (size_t type_param_index = 0; type_param_index < fun->type_params.len; type_param_index++) {
	      const Param *type_param = &fun->type_params.items[type_param_index];
	      scope_add(&scope, type_param->name, type_param->is_static ? (type_param->type ? type_param->type : "usize") : "Type", false);
	    }
	    for (size_t param_index = 0; param_index < fun->params.len; param_index++) {
	      Param *param = &fun->params.items[param_index];
      if (!validate_type_form(param->type, diag, param->line, param->column)) {
        scope_free(&scope);
        return false;
      }
      if (!validate_type_names(program, param->type, &fun->type_params, NULL, false, diag, param->line, param->column)) {
        scope_free(&scope);
        return false;
      }
      for (size_t previous = 0; previous < param_index; previous++) {
        if (strcmp(fun->params.items[previous].name, param->name) == 0) {
          scope_free(&scope);
          return set_diag_detail(diag, 3001, "duplicate parameter", param->line, param->column, "unique parameter name", "parameter name already declared", "rename one parameter");
        }
      }
      scope_add_param_decl(&scope, param->name, param->type, param->line, param->column);
    }
    for (size_t shape_index = 0; shape_index < program->shapes.len; shape_index++) scope_add(&scope, program->shapes.items[shape_index].name, "Type", false);
    for (size_t interface_index = 0; interface_index < program->interfaces.len; interface_index++) scope_add(&scope, program->interfaces.items[interface_index].name, "Type", false);
    for (size_t enum_index = 0; enum_index < program->enums.len; enum_index++) scope_add(&scope, program->enums.items[enum_index].name, "Type", false);
    for (size_t choice_index = 0; choice_index < program->choices.len; choice_index++) scope_add(&scope, program->choices.items[choice_index].name, "Type", false);
    for (size_t alias_index = 0; alias_index < program->aliases.len; alias_index++) scope_add(&scope, program->aliases.items[alias_index].name, "Type", false);
    checking_function = fun;
    bool ok = check_stmt_vec(program, fun, &fun->body, &scope, diag);
    if (ok) ok = check_function_has_required_return(fun, diag);
    checking_function = NULL;
    scope_free(&scope);
    if (!ok) return false;
  }
  return true;
}
