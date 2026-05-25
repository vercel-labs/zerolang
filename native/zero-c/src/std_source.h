#ifndef ZERO_C_STD_SOURCE_H
#define ZERO_C_STD_SOURCE_H

#include "zero.h"

typedef struct {
  const char *module;
  const char *path;
  const char *const *chunks;
} ZStdSourceModule;

const ZStdSourceModule *z_std_source_module_for_name(const char *module);
const ZStdSourceModule *z_std_source_module_for_public_call(const char *qualified_name);
const char *z_std_source_target_for_public_call(const char *qualified_name);
char *z_std_source_module_copy_source(const ZStdSourceModule *module);

#endif
