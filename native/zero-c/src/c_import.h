#ifndef ZERO_C_C_IMPORT_H
#define ZERO_C_C_IMPORT_H

#include "zero.h"

typedef struct {
  char *name;
  char *c_type;
  char *zero_type;
} ZCImportParam;

typedef struct {
  char *name;
  char *return_c_type;
  char *return_zero_type;
  ZCImportParam *params;
  size_t param_len;
  size_t param_cap;
} ZCImportFunction;

typedef struct {
  ZCImportFunction *items;
  size_t len;
  size_t cap;
} ZCImportFunctionVec;

void z_c_import_function_free(ZCImportFunction *function);
void z_c_import_function_vec_free(ZCImportFunctionVec *vec);
bool z_c_type_to_zero(const char *c_type, char *out, size_t out_len);
bool z_c_header_parse_functions(const char *header, ZCImportFunctionVec *out);
bool z_c_import_alias_exists(const Program *program, const char *alias);
bool z_c_import_find_function(const Program *program, const char *alias, const char *symbol, ZCImportFunction *out, ZDiag *diag);

#endif
