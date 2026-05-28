#include "program_graph_command.h"

#include <stddef.h>
#include <string.h>

typedef struct {
  const char *name;
  ZProgramGraphInputMode input_mode;
  bool supports_out;
  ZProgramGraphOutputContract out_contract;
} ZProgramGraphCommandKind;

#define GRAPH_OUT(name, input_mode) \
  {name, input_mode, true, {true, NULL, NULL, NULL, NULL}}
#define GRAPH_NO_OUT(name, input_mode, message, expected, actual, help) \
  {name, input_mode, false, {false, message, expected, actual, help}}

static const ZProgramGraphCommandKind z_graph_command_kinds[] = {
  GRAPH_OUT("dump", Z_PROGRAM_GRAPH_INPUT_SOURCE),
  GRAPH_OUT("import", Z_PROGRAM_GRAPH_INPUT_SOURCE),
  GRAPH_NO_OUT(
    "inspect",
    Z_PROGRAM_GRAPH_INPUT_SOURCE,
    "graph inspect does not support --out",
    "zero graph inspect [--json] <file.0|file.row|project|zero.json>",
    "zero graph inspect --out",
    "use zero graph dump or zero graph import with --out when you need a derived ProgramGraph artifact"
  ),
  GRAPH_OUT("validate", Z_PROGRAM_GRAPH_INPUT_ARTIFACT),
  GRAPH_OUT("view", Z_PROGRAM_GRAPH_INPUT_SOURCE_OR_ARTIFACT),
  GRAPH_NO_OUT(
    "check",
    Z_PROGRAM_GRAPH_INPUT_SOURCE_OR_ARTIFACT,
    "graph check does not support --out",
    "zero graph view --out <file.0> <program-graph-or-source>",
    "zero graph check --out",
    "run zero graph view to render canonical source, or run zero graph check without --out to typecheck the ProgramGraph input"
  ),
  GRAPH_OUT("size", Z_PROGRAM_GRAPH_INPUT_ARTIFACT),
  GRAPH_OUT("build", Z_PROGRAM_GRAPH_INPUT_ARTIFACT),
  GRAPH_OUT("run", Z_PROGRAM_GRAPH_INPUT_ARTIFACT),
  GRAPH_NO_OUT(
    "test",
    Z_PROGRAM_GRAPH_INPUT_ARTIFACT,
    "graph test does not support --out",
    "zero graph test [--json] [--filter <name>] [--target <target>] <program-graph-or-package>",
    "zero graph test --out",
    "test results are reported on stdout; remove --out"
  ),
  GRAPH_OUT("patch", Z_PROGRAM_GRAPH_INPUT_SOURCE_OR_ARTIFACT),
  GRAPH_OUT("roundtrip", Z_PROGRAM_GRAPH_INPUT_SOURCE_OR_ARTIFACT),
};

#undef GRAPH_OUT
#undef GRAPH_NO_OUT

static const ZProgramGraphCommandKind *graph_kind(const char *kind) {
  for (size_t i = 0; kind && i < sizeof(z_graph_command_kinds) / sizeof(z_graph_command_kinds[0]); i++) {
    if (strcmp(kind, z_graph_command_kinds[i].name) == 0) return &z_graph_command_kinds[i];
  }
  return NULL;
}

bool z_program_graph_command_kind_is_known(const char *kind) {
  return graph_kind(kind) != NULL;
}

ZProgramGraphInputMode z_program_graph_command_input_mode(const char *kind) {
  const ZProgramGraphCommandKind *item = graph_kind(kind);
  return item ? item->input_mode : Z_PROGRAM_GRAPH_INPUT_UNKNOWN;
}

bool z_program_graph_command_kind_supports_out(const char *kind) {
  const ZProgramGraphCommandKind *item = graph_kind(kind);
  return item && item->supports_out;
}

ZProgramGraphOutputContract z_program_graph_command_output_contract(const char *kind) {
  static const ZProgramGraphOutputContract fallback = {
    false,
    "graph requires an output-capable subcommand for --out",
    "zero graph dump|import|validate|patch|roundtrip --out <program-graph-artifact> <input>",
    "zero graph --out",
    "use zero graph view --out <file.0> for canonical source, or choose a graph subcommand with command-specific output",
  };
  const ZProgramGraphCommandKind *item = graph_kind(kind);
  return item ? item->out_contract : fallback;
}
