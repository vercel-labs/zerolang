#ifndef ZERO_C_PROGRAM_GRAPH_COMMAND_H
#define ZERO_C_PROGRAM_GRAPH_COMMAND_H

#include <stdbool.h>

typedef struct {
  bool supports_out;
  const char *message;
  const char *expected;
  const char *actual;
  const char *help;
} ZProgramGraphOutputContract;

typedef enum {
  Z_PROGRAM_GRAPH_INPUT_UNKNOWN = 0, Z_PROGRAM_GRAPH_INPUT_SOURCE,
  Z_PROGRAM_GRAPH_INPUT_ARTIFACT, Z_PROGRAM_GRAPH_INPUT_SOURCE_OR_ARTIFACT,
} ZProgramGraphInputMode;

bool z_program_graph_command_kind_is_known(const char *kind);
ZProgramGraphInputMode z_program_graph_command_input_mode(const char *kind);
bool z_program_graph_command_kind_supports_out(const char *kind);
ZProgramGraphOutputContract z_program_graph_command_output_contract(const char *kind);
void z_program_graph_print_command_help(void);

#endif
