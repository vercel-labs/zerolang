#include "program_graph_string_map.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool pg_string_map_text_eq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static uint64_t pg_string_map_hash_text(const char *text) {
  uint64_t hash = 1469598103934665603ULL;
  for (const unsigned char *p = (const unsigned char *)(text ? text : ""); *p; p++) {
    hash ^= *p;
    hash *= 1099511628211ULL;
  }
  return hash;
}

void z_program_graph_string_map_init(ZProgramGraphStringMap *map, size_t expected) {
  size_t cap = 16;
  while (cap < expected * 2) cap *= 2;
  map->entries = z_checked_calloc(cap, sizeof(ZProgramGraphStringMapEntry));
  map->cap = cap;
  map->len = 0;
}

void z_program_graph_string_map_free(ZProgramGraphStringMap *map) {
  for (size_t i = 0; map->entries && i < map->cap; i++) {
    if (map->entries[i].used) free(map->entries[i].key);
  }
  free(map->entries);
  *map = (ZProgramGraphStringMap){0};
}

static ZProgramGraphStringMapEntry *pg_string_map_slot(const ZProgramGraphStringMap *map, uint64_t hash, const char *key) {
  size_t index = (size_t)(hash & (map->cap - 1));
  for (;;) {
    ZProgramGraphStringMapEntry *entry = &map->entries[index];
    if (!entry->used) return entry;
    if (entry->hash == hash && pg_string_map_text_eq(entry->key, key)) return entry;
    index = (index + 1) & (map->cap - 1);
  }
}

ZProgramGraphStringMapEntry *z_program_graph_string_map_find(const ZProgramGraphStringMap *map, const char *key) {
  if (!map->entries || map->len == 0) return NULL;
  ZProgramGraphStringMapEntry *entry = pg_string_map_slot(map, pg_string_map_hash_text(key), key);
  return entry->used ? entry : NULL;
}

static void pg_string_map_grow(ZProgramGraphStringMap *map) {
  ZProgramGraphStringMap grown;
  z_program_graph_string_map_init(&grown, map->cap);
  for (size_t i = 0; i < map->cap; i++) {
    if (!map->entries[i].used) continue;
    ZProgramGraphStringMapEntry *slot = pg_string_map_slot(&grown, map->entries[i].hash, map->entries[i].key);
    *slot = map->entries[i];
    grown.len++;
  }
  free(map->entries);
  *map = grown;
}

void z_program_graph_string_map_put(ZProgramGraphStringMap *map, const char *key, size_t value) {
  if (map->len * 2 >= map->cap) pg_string_map_grow(map);
  uint64_t hash = pg_string_map_hash_text(key);
  ZProgramGraphStringMapEntry *entry = pg_string_map_slot(map, hash, key);
  if (!entry->used) {
    *entry = (ZProgramGraphStringMapEntry){.hash = hash, .key = z_strdup(key ? key : ""), .used = true};
    map->len++;
  }
  entry->value = value;
}
