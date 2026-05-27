#ifndef ZERO_C_PROGRAM_GRAPH_PATCH_H
#define ZERO_C_PROGRAM_GRAPH_PATCH_H

#include "program_graph.h"

typedef struct {
  size_t index;
  int line;
  char *op;
  char *node;
  char *parent;
  char *from;
  char *to;
  char *edge;
  char *kind;
  char *target;
  char *field;
  char *expected;
  char *actual;
  char *value;
  char *name;
  char *type;
  char *path;
  size_t order;
  int line_value;
  int column_value;
  bool has_expected;
  bool has_order;
  bool has_line_value;
  bool has_column_value;
  bool has_public_value;
  bool has_mutable_value;
  bool has_static_value;
  bool has_fallible_value;
  bool has_export_c_value;
  bool public_value;
  bool mutable_value;
  bool static_value;
  bool fallible_value;
  bool export_c_value;
  bool ok;
  char code[16];
  char message[160];
} ZProgramGraphPatchOpResult;

typedef struct {
  bool ok;
  char code[16];
  char message[160];
  char *expected;
  char *actual;
  char *expected_graph_hash;
  char *actual_graph_hash;
  ZProgramGraphPatchOpResult *operations;
  size_t operation_len;
  size_t operation_cap;
} ZProgramGraphPatchResult;

bool z_program_graph_patch_apply_operation(ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZProgramGraphPatchOpResult *op);
bool z_program_graph_apply_patch_text(const char *label, const char *text, size_t text_len, ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZDiag *diag);
bool z_program_graph_apply_patch_file(const char *path, ZProgramGraph *graph, ZProgramGraphPatchResult *result, ZDiag *diag);
void z_program_graph_patch_result_free(ZProgramGraphPatchResult *result);
#endif
