#ifndef ZERO_C_STD_SIG_H
#define ZERO_C_STD_SIG_H

#include <stdbool.h>
#include <stddef.h>

#define Z_STD_HELPER_MAX_ARGS 4
#define Z_STD_HELPER_MAX_ERRORS 4

typedef struct {
  const char *name;
  const char *return_type;
  int arg_count;
  const char *arg_types[Z_STD_HELPER_MAX_ARGS];
  const char *error_names[Z_STD_HELPER_MAX_ERRORS];
  const char *capability;
  const char *target_support;
  const char *allocation_behavior;
  bool emits_runtime_helper;
} ZStdHelperInfo;

extern const ZStdHelperInfo z_std_helpers[];

const ZStdHelperInfo *z_std_helper_find(const char *name);
int z_std_helper_index(const char *name, size_t max_helpers);
const char *z_std_helper_arg_type(const char *name, size_t index);
const char *z_std_helper_error_name(const ZStdHelperInfo *helper, size_t index);
bool z_std_helper_is_fallible(const ZStdHelperInfo *helper);
void z_std_helper_error_set_text(const ZStdHelperInfo *helper, char *buf, size_t cap);

#endif
