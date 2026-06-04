#include "program_graph_command.h"

#include <stddef.h>
#include <stdio.h>
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
    "zero graph inspect [--json] <file.0|project|zero.json>",
    "zero graph inspect --out",
    "use zero graph dump or zero graph import with --out when you need a derived ProgramGraph artifact"
  ),
  GRAPH_OUT("validate", Z_PROGRAM_GRAPH_INPUT_ARTIFACT),
  GRAPH_OUT("view", Z_PROGRAM_GRAPH_INPUT_SOURCE_OR_ARTIFACT),
  GRAPH_NO_OUT(
    "source-map",
    Z_PROGRAM_GRAPH_INPUT_SOURCE_OR_ARTIFACT,
    "graph source-map does not support --out",
    "zero graph source-map --json <program-graph-or-source>",
    "zero graph source-map --out",
    "source maps are reported on stdout; remove --out"
  ),
  GRAPH_NO_OUT(
    "reconcile",
    Z_PROGRAM_GRAPH_INPUT_SOURCE_OR_ARTIFACT,
    "graph reconcile does not support --out",
    "zero graph reconcile [--json] <base-program-graph-or-source> --source <edited-file.0|project|zero.json>",
    "zero graph reconcile --out",
    "reconciliation reports identity decisions on stdout; remove --out"
  ),
  GRAPH_NO_OUT("status", Z_PROGRAM_GRAPH_INPUT_SOURCE, "graph status does not support --out", "zero graph status [--json] <project|zero.json|file.0>", "zero graph status --out", "status is reported on stdout; remove --out"),
  GRAPH_NO_OUT("verify-sync", Z_PROGRAM_GRAPH_INPUT_SOURCE, "graph verify-sync does not support --out", "zero graph verify-sync [--json] <project|zero.json|file.0>", "zero graph verify-sync --out", "verify-sync is a no-write check; remove --out"),
  GRAPH_NO_OUT("sync", Z_PROGRAM_GRAPH_INPUT_SOURCE, "graph sync writes fixed repository paths and does not support --out", "zero graph sync (--from-source|--from-graph) <project|zero.json|file.0>", "zero graph sync --out", "choose --from-source or --from-graph; sync writes repository graph/source paths when enabled"),
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
  GRAPH_OUT("diff", Z_PROGRAM_GRAPH_INPUT_SOURCE_OR_ARTIFACT),
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

void z_program_graph_print_command_help(void) {
  printf("Usage: zero graph [dump|import|inspect|validate|view|source-map|reconcile|status|verify-sync|sync|check|size|build|run|test|patch|roundtrip] [--json] [--target <target>] <input> [patch]\n\n");
  printf("Output usage: zero graph [dump|import|validate|roundtrip] [--json] --out <program-graph-artifact> <input>\n");
  printf("View output usage: zero graph view [--json] [--out <file.0>] <program-graph-or-source>\n");
  printf("Source map usage: zero graph source-map --json <program-graph-or-source>\n");
  printf("Reconcile usage: zero graph reconcile [--json] <base-program-graph-or-source> --source <edited-file.0|project|zero.json>\n");
  printf("Repository sync usage: zero graph status|verify-sync [--json] <project|zero.json|file.0>; zero graph sync (--from-source|--from-graph) [--json] <project|zero.json|file.0>\n");
  printf("Size output usage: zero graph size [--json] [--target <target>] --out <artifact> <input>\n");
  printf("Patch output usage: zero graph patch [--json] [--out <program-graph-artifact>] <program-graph-or-source> (<patch-file>|--op <operation>)\n\n");
  printf("Build usage: zero graph build [--json] [--emit exe|obj|llvm-ir] [--backend direct|llvm|<direct-emitter>] [--target <target>] [--profile debug|dev|release-fast|release-small|tiny|audit] [--release <profile>] [--out <file>] <program-graph-or-package>\n\n");
  printf("Run usage: zero graph run [--target <host-target>] [--profile debug|dev|release-fast|release-small|tiny|audit] [--release <profile>] [--out <file>] <program-graph-or-package> [-- args...]\n\n");
  printf("Test usage: zero graph test [--json] [--filter <name>] [--target <target>] <program-graph-or-package>\n\n");
  printf("Inspect modules, symbols, capabilities, static metadata, stdlib helpers, or deterministic ProgramGraph inputs.\n\n");
  printf("Subcommands:\n");
  printf("  dump      print or write only the deterministic ProgramGraph\n");
  printf("  import    convert current Zero source into deterministic ProgramGraph input\n");
  printf("  inspect   report semantic graph and compiler facts as JSON\n");
  printf("  validate  read ProgramGraph input and optionally write its normalized artifact form\n");
  printf("  view      render ProgramGraph input as a generated Zero view\n");
  printf("  source-map map graph nodes to source ranges and semantic identity facts\n");
  printf("  reconcile compare a prior graph with edited source and report identity decisions\n");
  printf("  status    report repository graph sync state without writing files\n");
  printf("  verify-sync check graph/source projection sync without writing files\n");
  printf("  sync      synchronize repository graph and source projections when enabled\n");
  printf("  check     typecheck ProgramGraph input through direct graph lowering\n");
  printf("  size      report size, helper, runtime, and backend facts for ProgramGraph input\n");
  printf("  build     build ProgramGraph input through direct graph lowering\n  run       build and run ProgramGraph input through direct graph lowering\n  test      run test blocks from ProgramGraph input through direct graph lowering\n");
  printf("  patch     apply checked edits to ProgramGraph input\n");
  printf("  roundtrip compare graph semantics after direct ProgramGraph lowering\n");
}
