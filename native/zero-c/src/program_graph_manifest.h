#ifndef ZERO_C_PROGRAM_GRAPH_MANIFEST_H
#define ZERO_C_PROGRAM_GRAPH_MANIFEST_H

#include "zero.h"

#include <stdbool.h>

bool z_program_graph_manifest_compiler_input_enabled(const char *input_path, bool *enabled, ZDiag *diag);
bool z_program_graph_manifest_command_can_use_compiler_input(const char *command);
bool z_program_graph_manifest_attach_metadata_to_input(SourceInput *input, const char *manifest_input, ZDiag *diag);
bool z_program_graph_manifest_module_identity(const char *input_path, char **out, ZDiag *diag);

#endif
