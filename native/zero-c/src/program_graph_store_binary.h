#ifndef ZERO_C_PROGRAM_GRAPH_STORE_BINARY_H
#define ZERO_C_PROGRAM_GRAPH_STORE_BINARY_H

#include "program_graph_store.h"

bool z_program_graph_store_bytes_are_binary(const unsigned char *data, size_t len);
void z_program_graph_store_append_binary(ZBuf *buf, const ZProgramGraph *graph, const ZProgramGraphStore *projections);
bool z_program_graph_store_parse_binary(const char *path, const unsigned char *data, size_t len, ZProgramGraphStore *out, ZDiag *diag);

#endif
