#ifndef ZERO_C_PROGRAM_GRAPH_STORE_H
#define ZERO_C_PROGRAM_GRAPH_STORE_H

#include "program_graph.h"

typedef struct {
  char *root;
  char *path;
  bool present;
  unsigned schema_version;
  char **source_paths;
  size_t source_path_len;
  size_t source_path_cap;
  char **projection_paths;
  char **projection_texts;
  size_t projection_len;
  size_t projection_cap;
  ZProgramGraph graph;
} ZProgramGraphStore;

void z_program_graph_store_init(ZProgramGraphStore *store);
void z_program_graph_store_free(ZProgramGraphStore *store);
char *z_program_graph_store_root_for_input(const char *input);
char *z_program_graph_store_path_for_root(const char *root);
bool z_program_graph_store_file_exists(const char *path);
bool z_program_graph_store_load_path(const char *path, ZProgramGraphStore *out, ZDiag *diag);
bool z_program_graph_store_load_for_input(const char *input, ZProgramGraphStore *out, ZDiag *diag);
bool z_program_graph_store_write_path(const char *path, const ZProgramGraphStore *store, ZDiag *diag);
bool z_program_graph_store_write_generated_path(const char *path, const ZProgramGraph *graph, ZProgramGraphStore *out, ZDiag *diag);
bool z_program_graph_store_save_path(const char *path, const ZProgramGraph *graph, ZDiag *diag);
bool z_program_graph_store_save_for_input(const char *input, const ZProgramGraph *graph, ZProgramGraphStore *out, ZDiag *diag);
bool z_program_graph_store_graph_matches_source(const ZProgramGraphStore *store, const ZProgramGraph *source_graph);
bool z_program_graph_store_source_path_is_local(const char *path);
const char *z_program_graph_store_projection_text(const ZProgramGraphStore *store, const char *path);

#endif
