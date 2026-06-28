#include "program_graph_projection.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  uint64_t offset;
  uint64_t len;
} FastStoreStringRef;

typedef struct {
  size_t source_count, projection_count;
  size_t node_count, edge_count;
  size_t strings_offset;
  size_t strings_len;
  size_t records_offset;
  bool has_source_hash;
  FastStoreStringRef source_hash_ref;
} FastStoreBinaryHeader;

static bool fast_store_get_u32(const unsigned char *data, size_t len, size_t *offset, uint32_t *out) {
  if (!data || !offset || !out || *offset > len || len - *offset < 4) return false;
  const unsigned char *bytes = data + *offset;
  *out = ((uint32_t)bytes[0]) |
         ((uint32_t)bytes[1] << 8) |
         ((uint32_t)bytes[2] << 16) |
         ((uint32_t)bytes[3] << 24);
  *offset += 4;
  return true;
}

static bool fast_store_get_u64(const unsigned char *data, size_t len, size_t *offset, uint64_t *out) {
  if (!data || !offset || !out || *offset > len || len - *offset < 8) return false;
  const unsigned char *bytes = data + *offset;
  uint64_t value = 0;
  for (unsigned i = 0; i < 8; i++) value |= (uint64_t)bytes[i] << (i * 8);
  *out = value;
  *offset += 8;
  return true;
}

static bool fast_store_get_count(const unsigned char *data, size_t len, size_t *offset, size_t *out) {
  uint64_t value = 0;
  if (!fast_store_get_u64(data, len, offset, &value) || value > (uint64_t)SIZE_MAX) return false;
  *out = (size_t)value;
  return true;
}

static bool fast_store_get_ref(const unsigned char *data, size_t len, size_t *offset, FastStoreStringRef *out) {
  return fast_store_get_u64(data, len, offset, &out->offset) &&
         fast_store_get_u64(data, len, offset, &out->len);
}

static bool fast_store_records_fit(const FastStoreBinaryHeader *header, size_t file_len) {
  if (!header || header->records_offset > file_len || header->strings_offset > file_len || header->records_offset > header->strings_offset) return false;
  uint64_t available = (uint64_t)(header->strings_offset - header->records_offset);
  uint64_t record_bytes = 0;
  const uint64_t sizes[] = {16, 32, 160, 64};
  const uint64_t counts[] = {header->source_count, header->projection_count, header->node_count, header->edge_count};
  for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
    if (counts[i] > (UINT64_MAX - record_bytes) / sizes[i]) return false;
    record_bytes += counts[i] * sizes[i];
  }
  return record_bytes <= available;
}

static bool fast_store_binary_header_from_bytes(const unsigned char *data, size_t len, FastStoreBinaryHeader *out) {
  static const unsigned char magic[8] = {'Z', 'R', 'G', 'B', 'I', 'N', '1', 0};
  if (!data || !out || len < sizeof(magic) || memcmp(data, magic, sizeof(magic)) != 0) return false;
  size_t offset = sizeof(magic);
  uint32_t schema = 0, flags = 0, validation_state = 0, reserved = 0;
  uint64_t next_id = 0, string_bytes = 0;
  FastStoreStringRef ignored_ref = {0};
  bool ok = fast_store_get_u32(data, len, &offset, &schema) &&
            fast_store_get_u32(data, len, &offset, &flags) &&
            fast_store_get_u32(data, len, &offset, &validation_state) &&
            fast_store_get_u32(data, len, &offset, &reserved) &&
            fast_store_get_count(data, len, &offset, &out->source_count) &&
            fast_store_get_count(data, len, &offset, &out->projection_count) &&
            fast_store_get_count(data, len, &offset, &out->node_count) &&
            fast_store_get_count(data, len, &offset, &out->edge_count) &&
            fast_store_get_u64(data, len, &offset, &next_id) &&
            fast_store_get_u64(data, len, &offset, &string_bytes) &&
            fast_store_get_ref(data, len, &offset, &ignored_ref) &&
            fast_store_get_ref(data, len, &offset, &ignored_ref) &&
            fast_store_get_ref(data, len, &offset, &ignored_ref);
  if (ok) {
    out->has_source_hash = (flags & (1u << 1)) != 0;
    if (out->has_source_hash) ok = fast_store_get_ref(data, len, &offset, &out->source_hash_ref);
  }
  for (size_t i = 0; ok && i < 16; i++) {
    uint64_t ignored_count = 0;
    ok = fast_store_get_u64(data, len, &offset, &ignored_count);
  }
  (void)next_id;
  if (!ok || schema != 1 || reserved != 0 || (flags & ~3u) != 0 ||
      validation_state > (uint32_t)Z_PROGRAM_GRAPH_VALIDATION_BUILDABLE ||
      string_bytes > (uint64_t)len || string_bytes > (uint64_t)SIZE_MAX) {
    return false;
  }
  out->strings_len = (size_t)string_bytes;
  out->strings_offset = len - out->strings_len;
  out->records_offset = offset;
  return fast_store_records_fit(out, len);
}

static bool fast_store_ref_string(const unsigned char *strings, size_t strings_len, FastStoreStringRef ref, char **out) {
  if (out) *out = NULL;
  if (!strings || !out || ref.len == UINT64_MAX || ref.offset > (uint64_t)strings_len ||
      ref.len > (uint64_t)strings_len - ref.offset || ref.len > (uint64_t)SIZE_MAX) {
    return false;
  }
  const unsigned char *start = strings + (size_t)ref.offset;
  size_t len = (size_t)ref.len;
  if (memchr(start, 0, len) != NULL) return false;
  *out = z_strndup((const char *)start, len);
  return true;
}

static char *fast_store_join_path(const char *left, const char *right) {
  size_t left_len = strlen(left ? left : "");
  size_t right_len = strlen(right ? right : "");
  bool slash = left_len > 0 && left[left_len - 1] != '/' && left[left_len - 1] != '\\';
  char *path = z_checked_malloc(left_len + (slash ? 1u : 0u) + right_len + 1u);
  memcpy(path, left ? left : "", left_len);
  size_t offset = left_len;
  if (slash) path[offset++] = '/';
  memcpy(path + offset, right ? right : "", right_len);
  path[offset + right_len] = '\0';
  return path;
}

static char *fast_store_projection_file_text(const char *root, const char *source_path, bool *ok) {
  if (ok) *ok = false;
  if (!z_program_graph_store_source_path_is_local(source_path)) return NULL;
  bool before_exists = false;
  if (!z_program_graph_projection_source_path_safe_for_cached_read(root, source_path, &before_exists)) return NULL;
  if (!before_exists) { if (ok) *ok = true; return NULL; }
  char *path = fast_store_join_path(root && root[0] ? root : ".", source_path);
  char *text = z_read_file(path, NULL);
  free(path);
  if (ok) *ok = text != NULL;
  return text;
}

bool z_program_graph_projection_source_sync_state_binary_fast(const char *store_path, const char *root, ZProgramGraphProjectionSourceSync *sync_out) {
  if (sync_out) *sync_out = Z_PROGRAM_GRAPH_PROJECTION_SYNC_CLEAN;
  unsigned char *data = NULL;
  size_t len = 0;
  if (!z_read_binary_file(store_path, &data, &len, NULL)) return false;
  FastStoreBinaryHeader header = {0};
  bool ok = fast_store_binary_header_from_bytes(data, len, &header);
  if (!ok) {
    free(data);
    return false;
  }
  if (header.projection_count == 0) {
    free(data);
    return false;
  }
  const unsigned char *strings = data + header.strings_offset;
  char *recorded_hash = NULL;
  if (header.has_source_hash && !fast_store_ref_string(strings, header.strings_len, header.source_hash_ref, &recorded_hash)) {
    free(data);
    return false;
  }
  size_t cursor = header.records_offset + header.source_count * 16u;
  bool any_changed = false;
  uint64_t disk_state = z_program_graph_store_source_hash_seed();
  uint64_t table_state = z_program_graph_store_source_hash_seed();
  for (size_t i = 0; i < header.projection_count; i++) {
    FastStoreStringRef path_ref = {0};
    FastStoreStringRef text_ref = {0};
    char *projection_path = NULL;
    char *projection_text = NULL;
    char *current = NULL;
    ok = fast_store_get_ref(data, len, &cursor, &path_ref) &&
         fast_store_get_ref(data, len, &cursor, &text_ref) &&
         fast_store_ref_string(strings, header.strings_len, path_ref, &projection_path) &&
         fast_store_ref_string(strings, header.strings_len, text_ref, &projection_text) &&
         z_program_graph_store_source_path_is_local(projection_path);
    if (ok) {
      current = fast_store_projection_file_text(root, projection_path, &ok);
      if (ok) {
        if (!current || strcmp(current, projection_text ? projection_text : "") != 0) any_changed = true;
        disk_state = z_program_graph_store_source_hash_fold(disk_state, projection_path, current);
        table_state = z_program_graph_store_source_hash_fold(table_state, projection_path, projection_text ? projection_text : "");
      }
    }
    free(current);
    free(projection_path);
    free(projection_text);
    if (!ok) {
      free(recorded_hash);
      free(data);
      return false;
    }
  }
  if (!any_changed) {
    free(recorded_hash);
    free(data);
    if (sync_out) *sync_out = Z_PROGRAM_GRAPH_PROJECTION_SYNC_CLEAN;
    return true;
  }
  if (!recorded_hash || !recorded_hash[0]) {
    free(recorded_hash);
    free(data);
    return false;
  }
  char *disk_hash = z_program_graph_store_source_hash_text(disk_state);
  char *table_hash = z_program_graph_store_source_hash_text(table_state);
  if (sync_out) {
    if (strcmp(disk_hash, recorded_hash) == 0) *sync_out = Z_PROGRAM_GRAPH_PROJECTION_SYNC_STORE_NEWER;
    else if (strcmp(table_hash, recorded_hash) == 0) *sync_out = Z_PROGRAM_GRAPH_PROJECTION_SYNC_SOURCE_NEWER;
    else *sync_out = Z_PROGRAM_GRAPH_PROJECTION_SYNC_DIVERGED;
  }
  free(disk_hash);
  free(table_hash);
  free(recorded_hash);
  free(data);
  return true;
}
