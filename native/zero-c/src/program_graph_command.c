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
  GRAPH_NO_OUT(
    "init",
    Z_PROGRAM_GRAPH_INPUT_PATH,
    "graph init writes repository files and does not support --out",
    "zero graph init [--json] <project-path>",
    "zero graph init --out",
    "graph init writes zero.json and zero.graph at the selected project path; remove --out"
  ),
  GRAPH_OUT("dump", Z_PROGRAM_GRAPH_INPUT_SOURCE_OR_ARTIFACT),
  GRAPH_OUT("import", Z_PROGRAM_GRAPH_INPUT_SOURCE_OR_ARTIFACT),
  GRAPH_NO_OUT(
    "query",
    Z_PROGRAM_GRAPH_INPUT_SOURCE_OR_ARTIFACT,
    "graph query does not support --out",
    "zero graph query [--json] [--fn <name>] [--find <text>] [--refs <name>] [--calls <name>] [--node <id>] <program-graph-or-source>",
    "zero graph query --out",
    "queries are reported on stdout; remove --out"
  ),
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
    "zero graph source-map [--json] <program-graph-or-source>",
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
  GRAPH_NO_OUT("merge", Z_PROGRAM_GRAPH_INPUT_SOURCE, "graph merge writes the target zero.graph and does not support --out", "zero graph merge --base <base-zero.graph> --left <left-zero.graph> --right <right-zero.graph> <project|zero.json|file.0>", "zero graph merge --out", "merge writes the repository graph store selected by the input path; remove --out"),
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
  static const ZProgramGraphOutputContract check_contract = {
    false,
    "zero check does not support --out",
    "zero graph view --out <file.0> <program-graph-or-source>",
    "zero check --out",
    "run zero graph view to render canonical source, or run zero check without --out to typecheck the ProgramGraph input",
  };
  static const ZProgramGraphOutputContract fallback = {
    false,
    "graph requires an output-capable subcommand for --out",
    "zero graph dump|import|validate|roundtrip --out <program-graph-artifact> <input>",
    "zero graph --out",
    "use zero graph view --out <file.0> for canonical source, or choose a graph subcommand with command-specific output",
  };
  if (strcmp(kind ? kind : "", "check") == 0) return check_contract;
  const ZProgramGraphCommandKind *item = graph_kind(kind);
  return item ? item->out_contract : fallback;
}

void z_program_graph_print_command_help(void) {
  printf("Usage: zero graph [init|dump|import|query|inspect|validate|view|source-map|reconcile|status|verify-sync|sync|merge|size|build|run|test|roundtrip] [--json] [--target <target>] <input>\n\n");
  printf("Graph-first project usage: zero graph init [--json] <project-path>\n");
  printf("Output usage: zero graph [dump|import|validate|roundtrip] [--json] --out <program-graph-artifact> <input>\n");
  printf("View output usage: zero graph view [--json] [--out <file.0>] <program-graph-or-source>\n");
  printf("Source map usage: zero graph source-map [--json] <program-graph-or-source>\n");
  printf("Query usage: zero graph query [--json] [--fn <name>] [--find <text>] [--refs <name>] [--calls <name>] [--node <id>] <program-graph-or-source>\n");
  printf("Reconcile usage: zero graph reconcile [--json] <base-program-graph-or-source> --source <edited-file.0|project|zero.json>\n");
  printf("Repository sync usage: zero graph status|verify-sync [--json] <project|zero.json|file.0>; zero graph sync (--from-source|--from-graph) [--json] <project|zero.json|file.0>; zero graph merge --base <base-zero.graph> --left <left-zero.graph> --right <right-zero.graph> [--json] <project|zero.json|file.0>\n");
  printf("Size output usage: zero graph size [--json] [--target <target>] --out <artifact> <input>\n");
  printf("Patch usage: zero patch [--json] [--check-only|--dry-run] [--out <program-graph-artifact>] [<input>] (<patch-file>|--op <operation>)\n");
  printf("  In a graph-first package, zero patch --op <operation> defaults to the current directory.\nPatch operation help: zero patch --op help\n\n");
  printf("Build usage: zero graph build [--json] [--emit exe|obj|llvm-ir] [--backend direct|llvm|<direct-emitter>] [--target <target>] [--profile debug|dev|release-fast|release-small|tiny|audit] [--release <profile>] [--out <file>] <program-graph-or-package>\n\n");
  printf("Run usage: zero graph run [--target <host-target>] [--profile debug|dev|release-fast|release-small|tiny|audit] [--release <profile>] [--out <file>] <program-graph-or-package> [-- args...]\n\n");
  printf("Test usage: zero graph test [--json] [--filter <name>] [--target <target>] <program-graph-or-package>\n\n");
  printf("Inspect modules, symbols, capabilities, static metadata, stdlib helpers, or deterministic ProgramGraph inputs.\n\n");
  printf("Subcommands:\n");
  printf("  init      create a graph-first package with zero.graph as compiler input\n");
  printf("  dump      print or write only the deterministic ProgramGraph\n");
  printf("  import    convert current Zero source into deterministic ProgramGraph input\n");
  printf("  query     report compact module, function, body, and patch facts for agents\n");
  printf("  inspect   report semantic graph and compiler facts\n");
  printf("  validate  read ProgramGraph input and optionally write its normalized artifact form\n");
  printf("  view      render ProgramGraph input as a generated Zero view\n");
  printf("  source-map map graph nodes to source ranges and semantic identity facts\n");
  printf("  reconcile compare a prior graph with edited source and report identity decisions\n");
  printf("  status    report repository graph sync state without writing files\n");
  printf("  verify-sync check graph/source projection sync without writing files\n");
  printf("  sync      synchronize repository graph and source projections when enabled\n");
  printf("  merge     combine independent repository graph store edits by durable node id\n");
  printf("  size      report size, helper, runtime, and backend facts for ProgramGraph input\n");
  printf("  build     build ProgramGraph input through direct graph lowering\n  run       build and run ProgramGraph input through direct graph lowering\n  test      run test blocks from ProgramGraph input through direct graph lowering\n");
  printf("  roundtrip compare graph semantics after direct ProgramGraph lowering\n");
  printf("\nCommon patch operations:\n");
  printf("  addMain\n");
  printf("  addCheckWrite fn=\"main\" text=\"hello\\n\"\n");
  printf("  addFunction name=\"add\" ret=\"i32\"\n");
  printf("  addParam fn=\"add\" name=\"left\" type=\"i32\"\n");
  printf("  addReturnBinary fn=\"add\" name=\"+\" left=\"left\" right=\"right\" type=\"i32\"\n");
  printf("  addLetLiteral fn=\"main\" name=\"count\" type=\"u32\" value=\"0\"\n");
  printf("  addLetBinary fn=\"add\" name=\"sum\" type=\"i32\" operator=\"+\" left=\"left\" right=\"right\"\n");
  printf("  addReturnValue fn=\"identity\" value=\"input\" type=\"i32\"\n");
  printf("  addCheckWriteValue fn=\"main\" value=\"message\" type=\"String\"\n");
  printf("  addTest name=\"addition works\" call=\"add\" arg0=\"40\" arg1=\"2\" expect=\"42\" type=\"i32\"\n");
  printf("  setMainArgsAddCli fn=\"add_u32\"\n");
  printf("  setMainGreetingCli prefix=\"hello \" fallback=\"anonymous\"\n");
  printf("  replaceFunctionBody main ... end\n");
}
