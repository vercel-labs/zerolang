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
    "init writes repository files and does not support --out",
    "zero init [--json] [--format text|binary] <project-path>",
    "zero init --out",
    "init writes zero.json and zero.graph at the selected project path; remove --out"
  ),
  GRAPH_OUT("dump", Z_PROGRAM_GRAPH_INPUT_SOURCE_OR_ARTIFACT),
  GRAPH_OUT("import", Z_PROGRAM_GRAPH_INPUT_SOURCE_OR_ARTIFACT),
  GRAPH_NO_OUT(
    "query",
    Z_PROGRAM_GRAPH_INPUT_SOURCE_OR_ARTIFACT,
    "query does not support --out",
    "zero query [--json] [--fn <name>] [--find <text>] [--refs <name>] [--calls <name>] [--node <id>] <program-graph-or-source>",
    "zero query --out",
    "queries are reported on stdout; remove --out"
  ),
  GRAPH_NO_OUT(
    "inspect",
    Z_PROGRAM_GRAPH_INPUT_SOURCE,
    "inspect does not support --out",
    "zero inspect [--json] <file.0|project|zero.json>",
    "zero inspect --out",
    "use zero dump or zero import with --out when you need a derived ProgramGraph artifact"
  ),
  GRAPH_OUT("validate", Z_PROGRAM_GRAPH_INPUT_ARTIFACT),
  GRAPH_OUT("view", Z_PROGRAM_GRAPH_INPUT_SOURCE_OR_ARTIFACT),
  GRAPH_NO_OUT(
    "source-map",
    Z_PROGRAM_GRAPH_INPUT_SOURCE_OR_ARTIFACT,
    "source-map does not support --out",
    "zero source-map [--json] <program-graph-or-source>",
    "zero source-map --out",
    "source maps are reported on stdout; remove --out"
  ),
  GRAPH_NO_OUT(
    "reconcile",
    Z_PROGRAM_GRAPH_INPUT_SOURCE_OR_ARTIFACT,
    "reconcile does not support --out",
    "zero reconcile [--json] <base-program-graph-or-source> --source <edited-file.0|project|zero.json>",
    "zero reconcile --out",
    "reconciliation reports identity decisions on stdout; remove --out"
  ),
  GRAPH_NO_OUT("status", Z_PROGRAM_GRAPH_INPUT_SOURCE, "status does not support --out", "zero status [--json] <project|zero.json|file.0>", "zero status --out", "status is reported on stdout; remove --out"),
  GRAPH_NO_OUT("verify-sync", Z_PROGRAM_GRAPH_INPUT_SOURCE, "verify-sync does not support --out", "zero verify-sync [--json] <project|zero.json|file.0>", "zero verify-sync --out", "verify-sync is a no-write check; remove --out"),
  GRAPH_NO_OUT("sync", Z_PROGRAM_GRAPH_INPUT_SOURCE, "sync writes fixed repository paths and does not support --out", "zero sync (--from-source|--from-graph) [--format text|binary] <project|zero.json|file.0>", "zero sync --out", "choose --from-source or --from-graph; sync writes repository graph/source paths when enabled"),
  GRAPH_NO_OUT("merge", Z_PROGRAM_GRAPH_INPUT_SOURCE, "merge writes the target zero.graph and does not support --out", "zero merge --base <base-zero.graph> --left <left-zero.graph> --right <right-zero.graph> [--format text|binary] <project|zero.json|file.0>", "zero merge --out", "merge writes the repository graph store selected by the input path; remove --out"),
  GRAPH_OUT("size", Z_PROGRAM_GRAPH_INPUT_ARTIFACT),
  GRAPH_OUT("build", Z_PROGRAM_GRAPH_INPUT_ARTIFACT),
  GRAPH_OUT("run", Z_PROGRAM_GRAPH_INPUT_ARTIFACT),
  GRAPH_NO_OUT(
    "test",
    Z_PROGRAM_GRAPH_INPUT_ARTIFACT,
    "zero test does not support --out",
    "zero test [--json] [--filter <name>] [--target <target>] <program-graph-artifact>",
    "zero test --out",
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
    "zero view --out <file.0> <program-graph-or-source>",
    "zero check --out",
    "run zero view to render canonical source, or run zero check without --out to typecheck the ProgramGraph input",
  };
  static const ZProgramGraphOutputContract fallback = {
    false,
    "output requires an output-capable graph command",
    "zero dump|import|validate|roundtrip --out <program-graph-artifact> <input>",
    "zero --out",
    "use zero view --out <file.0> for canonical source, or choose a graph subcommand with command-specific output",
  };
  if (strcmp(kind ? kind : "", "check") == 0) return check_contract;
  const ZProgramGraphCommandKind *item = graph_kind(kind);
  return item ? item->out_contract : fallback;
}

void z_program_graph_print_command_help(void) {
  printf("Usage: zero init|query|view|status|verify-sync|sync|dump|import|inspect|validate|source-map|reconcile|merge|roundtrip [--json] <input>\n\n");
  printf("Graph-first project usage: zero init [--json] <project-path>\n");
  printf("Output usage: zero dump|import|validate|roundtrip [--json] --out <program-graph-artifact> <input>\n");
  printf("View output usage: zero view [--json] [--out <file.0>] <program-graph-or-source>\n");
  printf("Source map usage: zero source-map [--json] <program-graph-or-source>\n");
  printf("Query usage: zero query [--json] [--fn <name>] [--find <text>] [--refs <name>] [--calls <name>] [--node <id>] <program-graph-or-source>\n");
  printf("Reconcile usage: zero reconcile [--json] <base-program-graph-or-source> --source <edited-file.0|project|zero.json>\n");
  printf("Repository sync usage: zero status|verify-sync [--json] <project|zero.json|file.0>; zero sync (--from-source|--from-graph) [--json] <project|zero.json|file.0>; zero merge --base <base-zero.graph> --left <left-zero.graph> --right <right-zero.graph> [--json] <project|zero.json|file.0>\n");
  printf("Size usage: zero size [--json] [--target <target>] [--out <artifact>] <program-graph-artifact>\n");
  printf("Patch usage: zero patch [--json] [--check-only|--dry-run] [--out <program-graph-artifact>] [<input>] (<patch-file>|--op <operation>)\n");
  printf("  In a graph-first package, zero patch --op <operation> defaults to the current directory.\nPatch operation help: zero patch --op help\n\n");
  printf("Build usage: zero build [--json] [--emit exe|obj|llvm-ir] [--backend direct|llvm|<direct-emitter>] [--target <target>] [--profile debug|dev|release-fast|release-small|tiny|audit] [--release <profile>] [--out <file>] <program-graph-artifact>\n\n");
  printf("Run usage: zero run [--target <host-target>] [--profile debug|dev|release-fast|release-small|tiny|audit] [--release <profile>] [--out <file>] <program-graph-artifact> [-- args...]\n\n");
  printf("Test usage: zero test [--json] [--filter <name>] [--target <target>] <program-graph-artifact>\n\n");
  printf("Inspect modules, symbols, capabilities, static metadata, stdlib helpers, or deterministic ProgramGraph inputs.\n\n");
  printf("Graph commands:\n");
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
  printf("  roundtrip compare graph semantics after direct ProgramGraph lowering\n");
  printf("\nRepository store encoding:\n");
  printf("  text is the default zero.graph encoding\n");
  printf("  --format binary opts init, patch, sync --from-source, or merge into binary zero.graph writes\n");
  printf("  reads auto-detect text and binary stores\n");
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
  printf("  replaceFunctionBody main ... end\n");
  printf("  replaceBlockBody #block_id ... end\n");
}
