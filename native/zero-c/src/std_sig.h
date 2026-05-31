#ifndef ZERO_C_STD_SIG_H
#define ZERO_C_STD_SIG_H

#include <stdbool.h>
#include <stddef.h>

#define Z_STD_HELPER_MAX_ARGS 4
#define Z_STD_HELPER_MAX_ERRORS 4

typedef enum {
  Z_STD_HELPER_KIND_TABLE,
  Z_STD_HELPER_KIND_MEM_LEN,
  Z_STD_HELPER_KIND_MEM_GET,
  Z_STD_HELPER_KIND_MEM_EQL_BYTES,
  Z_STD_HELPER_KIND_MEM_COPY_ITEMS,
  Z_STD_HELPER_KIND_MEM_FILL_ITEMS,
  Z_STD_HELPER_KIND_MEM_CONTAINS,
  Z_STD_HELPER_KIND_MEM_IS_EMPTY,
  Z_STD_HELPER_KIND_MEM_SLICE,
  Z_STD_HELPER_KIND_MEM_ALLOC_BYTES,
  Z_STD_HELPER_KIND_MEM_BYTE_BUF,
  Z_STD_HELPER_KIND_FS_READ,
  Z_STD_HELPER_KIND_FS_READ_ALL,
  Z_STD_HELPER_KIND_FS_READ_ALL_OR_RAISE,
  Z_STD_HELPER_KIND_JSON_PARSE,
  Z_STD_HELPER_KIND_JSON_PARSE_BYTES,
  Z_STD_HELPER_KIND_UNKNOWN
} ZStdHelperKind;

typedef struct ZStdHelperInfo {
  const char *name;
  const char *return_type;
  int arg_count;
  const char *arg_types[Z_STD_HELPER_MAX_ARGS];
  const char *error_names[Z_STD_HELPER_MAX_ERRORS];
  const char *capability;
  const char *target_support;
  const char *allocation_behavior;
  bool emits_runtime_helper;
  ZStdHelperKind kind;
} ZStdHelperInfo;

extern const ZStdHelperInfo z_std_helpers[];

const ZStdHelperInfo *z_std_helper_find(const char *name);
int z_std_helper_index(const char *name, size_t max_helpers);
ZStdHelperKind z_std_helper_kind(const ZStdHelperInfo *helper);
const char *z_std_helper_arg_type(const char *name, size_t index);
const char *z_std_helper_error_name(const ZStdHelperInfo *helper, size_t index);
bool z_std_helper_is_fallible(const ZStdHelperInfo *helper);
void z_std_helper_error_set_text(const ZStdHelperInfo *helper, char *buf, size_t cap);

#endif
