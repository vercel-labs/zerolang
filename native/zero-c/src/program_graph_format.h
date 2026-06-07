#ifndef ZERO_C_PROGRAM_GRAPH_FORMAT_H
#define ZERO_C_PROGRAM_GRAPH_FORMAT_H

#include "program_graph.h"
#include "program_graph_store.h"

void z_program_graph_append_json(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphValidation *validation);
void z_program_graph_append_dump(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphValidation *validation);
bool z_program_graph_parse_dump(const char *text, ZProgramGraph *out, ZDiag *diag);
bool z_program_graph_load(const char *path, ZProgramGraph *out, ZDiag *diag);
static inline bool z_program_graph_path_is_source_text(const char *path) {
  const char *end = path;
  if (!end) return false;
  while (*end) end++;
  return end - path >= 2 && end[-2] == '.' && end[-1] == '0';
}
bool z_program_graph_save(const char *path, const ZProgramGraph *graph, ZDiag *diag);
bool z_program_graph_save_format(const char *path, const ZProgramGraph *graph, ZProgramGraphStoreFormat format, ZDiag *diag);
void z_append_program_graph_json(ZBuf *buf, const SourceInput *input, const Program *program);
void z_append_program_graph_dump(ZBuf *buf, const SourceInput *input, const Program *program, bool json);
#endif
