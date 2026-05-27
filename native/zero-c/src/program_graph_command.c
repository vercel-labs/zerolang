#include "program_graph_command.h"

#include <stddef.h>
#include <string.h>

typedef struct {
  const char *name;
  bool uses_artifact_input;
  bool supports_out;
  ZProgramGraphOutputContract out_contract;
} ZProgramGraphCommandKind;

#define GRAPH_OUT(name, artifact_input) \
  {name, artifact_input, true, {true, NULL, NULL, NULL, NULL}}
#define GRAPH_NO_OUT(name, artifact_input, message, expected, actual, help) \
  {name, artifact_input, false, {false, message, expected, actual, help}}

static const ZProgramGraphCommandKind z_graph_command_kinds[] = {
  GRAPH_OUT("dump", false),
  GRAPH_OUT("import", false),
  GRAPH_NO_OUT(
    "inspect",
    false,
    "graph inspect does not support --out",
    "zero graph inspect [--json] <file.0|file.row|project|zero.json>",
    "zero graph inspect --out",
    "use zero graph dump or zero graph import with --out when you need a ProgramGraph artifact"
  ),
  GRAPH_OUT("validate", true),
  GRAPH_OUT("view", true),
  GRAPH_NO_OUT(
    "check",
    true,
    "graph check does not write generated source views",
    "zero graph view --out <file.0> <graph-artifact-or-package>",
    "zero graph check --out",
    "run zero graph view to render a generated source view, or run zero graph check without --out to typecheck the artifact"
  ),
  GRAPH_OUT("size", true),
  GRAPH_OUT("build", true),
  GRAPH_OUT("run", true),
  GRAPH_NO_OUT(
    "test",
    true,
    "graph test does not support --out",
    "zero graph test [--json] [--filter <name>] [--target <target>] <graph-artifact-or-package>",
    "zero graph test --out",
    "test results are reported on stdout; remove --out"
  ),
  GRAPH_OUT("patch", true),
  GRAPH_OUT("roundtrip", false),
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

bool z_program_graph_command_kind_uses_artifact_input(const char *kind) {
  const ZProgramGraphCommandKind *item = graph_kind(kind);
  return item && item->uses_artifact_input;
}

bool z_program_graph_command_kind_supports_out(const char *kind) {
  const ZProgramGraphCommandKind *item = graph_kind(kind);
  return item && item->supports_out;
}

ZProgramGraphOutputContract z_program_graph_command_output_contract(const char *kind) {
  static const ZProgramGraphOutputContract fallback = {
    false,
    "graph requires an output-capable subcommand for --out",
    "zero graph dump|import|validate|patch|roundtrip --out <program-graph> <input>",
    "zero graph --out",
    "use zero graph view --out <file.0> for generated source views, or choose a graph subcommand with command-specific output",
  };
  const ZProgramGraphCommandKind *item = graph_kind(kind);
  return item ? item->out_contract : fallback;
}

bool z_program_graph_direct_command_uses_manifest_input(const char *command) {
  static const char *const commands[] = {"check", "build", "run", "test", "size", "ship"};
  for (size_t i = 0; command && i < sizeof(commands) / sizeof(commands[0]); i++) {
    if (strcmp(command, commands[i]) == 0) return true;
  }
  return false;
}
