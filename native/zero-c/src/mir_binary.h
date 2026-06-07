#ifndef ZERO_C_MIR_BINARY_H
#define ZERO_C_MIR_BINARY_H

#include "zero.h"

typedef struct {
  bool hit;
  bool mapped;
  size_t byte_len;
  char path[512];
} ZMirBinaryCacheFacts;

Z_RET_OWNED char *z_mir_binary_cache_path_for_graph_store(Z_IN const char *store_path,
                                                          Z_IN const char *graph_hash,
                                                          Z_IN const ZTargetInfo *target,
                                                          Z_IN const char *emit_kind,
                                                          Z_IN const char *backend);

bool z_mir_binary_write_path(Z_IN const char *path,
                             Z_IN const IrProgram *program,
                             Z_IN const char *graph_hash,
                             Z_IN const ZTargetInfo *target,
                             Z_IN const char *emit_kind,
                             Z_IN const char *backend,
                             Z_OUT ZDiag *diag);

bool z_mir_binary_load_path(Z_IN const char *path,
                            Z_IN const char *expected_graph_hash,
                            Z_IN const ZTargetInfo *target,
                            Z_IN const char *emit_kind,
                            Z_IN const char *backend,
                            Z_OUT IrProgram *out,
                            Z_OUT ZMirBinaryCacheFacts *facts,
                            Z_OUT ZDiag *diag);

#endif
