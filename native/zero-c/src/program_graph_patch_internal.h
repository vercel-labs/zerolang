#ifndef ZERO_C_PROGRAM_GRAPH_PATCH_INTERNAL_H
#define ZERO_C_PROGRAM_GRAPH_PATCH_INTERNAL_H

#include "program_graph_patch.h"

/* Shared between program_graph_patch.c and program_graph_patch_replace.c. */
void z_graph_patch_replace_text(char **slot, const char *value);
void z_graph_patch_result_fail(ZProgramGraphPatchResult *result, const char *code, const char *message, const char *expected, const char *actual);
ZProgramGraphPatchOpResult *z_graph_patch_push_operation(ZProgramGraphPatchResult *result);
bool z_graph_patch_apply_operations(ZProgramGraph *graph, ZProgramGraphPatchResult *result);
char *z_graph_patch_read_body_source(const char *path, size_t *out_len, ZDiag *diag);

#endif
