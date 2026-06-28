#ifndef ZERO_C_PROGRAM_GRAPH_STRING_MAP_H
#define ZERO_C_PROGRAM_GRAPH_STRING_MAP_H

#include "zero.h"
#include <stdint.h>

typedef struct {
  uint64_t hash;
  char *key;
  size_t value;
  bool used;
} ZProgramGraphStringMapEntry;

typedef struct {
  ZProgramGraphStringMapEntry *entries;
  size_t cap;
  size_t len;
} ZProgramGraphStringMap;

void z_program_graph_string_map_init(ZProgramGraphStringMap *map, size_t expected);
void z_program_graph_string_map_free(ZProgramGraphStringMap *map);
ZProgramGraphStringMapEntry *z_program_graph_string_map_find(const ZProgramGraphStringMap *map, const char *key);
void z_program_graph_string_map_put(ZProgramGraphStringMap *map, const char *key, size_t value);

#endif
