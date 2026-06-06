#ifndef ZERO_C_STD_SOURCE_H
#define ZERO_C_STD_SOURCE_H

#include "zero.h"

typedef struct {
  const char *module;
  const char *path;
  const char *const *chunks;
  const char *const *graph_chunks;
} ZStdSourceModule;

size_t z_std_source_module_count(void);
const ZStdSourceModule *z_std_source_module_at(size_t index);
const ZStdSourceModule *z_std_source_module_for_name(const char *module);
const ZStdSourceModule *z_std_source_module_for_public_call(const char *qualified_name);
const char *z_std_source_target_for_public_call(const char *qualified_name);
char *z_std_source_module_copy_source(const ZStdSourceModule *module);
char *z_std_source_module_copy_graph(const ZStdSourceModule *module);

#endif
