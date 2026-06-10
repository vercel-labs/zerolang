#include "program_graph_command.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef struct {
  const char *name;
  ZProgramGraphInputMode input_mode;
  bool repository_store_input;
  bool graph_subcommand;
  bool supports_out;
  ZProgramGraphOutputContract out_contract;
} ZProgramGraphCommandKind;

#define GRAPH_OUT(name, input_mode, repo) {name, input_mode, repo, true, true, {true, NULL, NULL, NULL, NULL}}
#define GRAPH_NO_OUT(name, input_mode, repo, message, expected, actual, help) {name, input_mode, repo, true, false, {false, message, expected, actual, help}}

static const ZProgramGraphCommandKind z_graph_command_kinds[] = {
  GRAPH_NO_OUT(
    "init",
    Z_PROGRAM_GRAPH_INPUT_PATH,
    false,
    "init writes repository files and does not support --out",
    "zero init [--json] [--format text|binary] [--template cli|lib|package] [project-path]",
    "zero init --out",
    "init writes zero.toml or zero.json plus zero.graph at the selected project path; remove --out"
  ),
  GRAPH_OUT("dump", Z_PROGRAM_GRAPH_INPUT_SOURCE_OR_ARTIFACT, true),
  GRAPH_OUT("import", Z_PROGRAM_GRAPH_INPUT_SOURCE_OR_ARTIFACT, false),
  {"check", Z_PROGRAM_GRAPH_INPUT_ARTIFACT, true, false, false, {false, "zero check does not support --out", "zero view --out <file.0> [graph-input]", "zero check --out", "run zero view to render canonical source, or run zero check without --out to typecheck the ProgramGraph input"}},
  GRAPH_NO_OUT(
    "query",
    Z_PROGRAM_GRAPH_INPUT_SOURCE_OR_ARTIFACT,
    true,
    "query does not support --out",
    "zero query [--json] [--fn <name>] [--find <text>] [--refs <name>] [--calls <name>] [--node <id>] [--depth <n>] [--full] [graph-input|name]",
    "zero query --out",
    "queries are reported on stdout; remove --out"
  ),
  GRAPH_NO_OUT(
    "inspect",
    Z_PROGRAM_GRAPH_INPUT_SOURCE_OR_ARTIFACT,
    true,
    "inspect does not support --out",
    "zero inspect [--json] [graph-input]",
    "zero inspect --out",
    "use zero dump or zero import with --out when you need a derived ProgramGraph artifact"
  ),
  GRAPH_OUT("validate", Z_PROGRAM_GRAPH_INPUT_SOURCE_OR_ARTIFACT, true),
  GRAPH_OUT("view", Z_PROGRAM_GRAPH_INPUT_SOURCE_OR_ARTIFACT, true),
  GRAPH_NO_OUT("diff", Z_PROGRAM_GRAPH_INPUT_SOURCE_OR_ARTIFACT, true, "diff textconv output does not support --out", "zero diff [graph-input]", "zero diff --out", "diff prints a canonical review projection on stdout; use zero view --out <file.0> when you need to write a projection file"),
  GRAPH_NO_OUT(
    "source-map",
    Z_PROGRAM_GRAPH_INPUT_SOURCE_OR_ARTIFACT,
    true,
    "source-map does not support --out",
    "zero source-map [--json] [graph-input]",
    "zero source-map --out",
    "source maps are reported on stdout; remove --out"
  ),
  GRAPH_NO_OUT(
    "reconcile",
    Z_PROGRAM_GRAPH_INPUT_SOURCE_OR_ARTIFACT,
    true,
    "reconcile does not support --out",
    "zero reconcile [--json] <base-graph-input> --source <edited-file.0|project|zero.toml|zero.json>",
    "zero reconcile --out",
    "reconciliation reports identity decisions on stdout; remove --out"
  ),
  GRAPH_NO_OUT("status", Z_PROGRAM_GRAPH_INPUT_SOURCE, false, "status does not support --out", "zero status [--json] [project|zero.toml|zero.json|file.0]", "zero status --out", "status is reported on stdout; remove --out"),
  GRAPH_NO_OUT("verify-projection", Z_PROGRAM_GRAPH_INPUT_SOURCE, false, "verify-projection does not support --out", "zero verify-projection [--json] [project|zero.toml|zero.json|file.0]", "zero verify-projection --out", "verify-projection is a no-write check; remove --out"),
  GRAPH_NO_OUT("export", Z_PROGRAM_GRAPH_INPUT_SOURCE, false, "export writes fixed source projection paths and does not support --out", "zero export [--json] [project|zero.toml|zero.json|file.0]", "zero export --out", "export writes repository source projections from zero.graph; remove --out"),
  GRAPH_NO_OUT("merge", Z_PROGRAM_GRAPH_INPUT_SOURCE, false, "merge writes the target zero.graph and does not support --out", "zero merge --base <base-zero.graph> --left <left-zero.graph> --right <right-zero.graph> [--format text|binary] [project|zero.toml|zero.json|file.0]", "zero merge --out", "merge writes the repository graph store selected by the input path; remove --out"),
  GRAPH_OUT("size", Z_PROGRAM_GRAPH_INPUT_ARTIFACT, true),
  GRAPH_OUT("build", Z_PROGRAM_GRAPH_INPUT_ARTIFACT, true),
  GRAPH_OUT("run", Z_PROGRAM_GRAPH_INPUT_ARTIFACT, true),
  GRAPH_OUT("patch", Z_PROGRAM_GRAPH_INPUT_SOURCE_OR_ARTIFACT, true),
  GRAPH_NO_OUT(
    "test",
    Z_PROGRAM_GRAPH_INPUT_ARTIFACT,
    true,
    "zero test does not support --out",
    "zero test [--json] [--filter <name>] [--target <target>] [graph-input]",
    "zero test --out",
    "test results are reported on stdout; remove --out"
  ),
  GRAPH_OUT("roundtrip", Z_PROGRAM_GRAPH_INPUT_SOURCE_OR_ARTIFACT, true),
};

#undef GRAPH_OUT
#undef GRAPH_NO_OUT

static const ZProgramGraphCommandKind *graph_kind(const char *kind) {
  for (size_t i = 0; kind && i < sizeof(z_graph_command_kinds) / sizeof(z_graph_command_kinds[0]); i++) {
    if (strcmp(kind, z_graph_command_kinds[i].name) == 0) return &z_graph_command_kinds[i];
  }
  return NULL;
}

bool z_program_graph_command_kind_is_known(const char *kind) { const ZProgramGraphCommandKind *item = graph_kind(kind); return item && item->graph_subcommand; }

ZProgramGraphInputMode z_program_graph_command_input_mode(const char *kind) { const ZProgramGraphCommandKind *item = graph_kind(kind); return item ? item->input_mode : Z_PROGRAM_GRAPH_INPUT_UNKNOWN; }

bool z_program_graph_command_can_use_repository_store(const char *kind) { const ZProgramGraphCommandKind *item = graph_kind(kind); return item && item->repository_store_input; }

bool z_program_graph_command_kind_supports_out(const char *kind) { const ZProgramGraphCommandKind *item = graph_kind(kind); return item && item->supports_out; }

ZProgramGraphOutputContract z_program_graph_command_output_contract(const char *kind) {
  static const ZProgramGraphOutputContract fallback = {
    false,
    "output requires an output-capable graph command",
    "zero dump|validate|roundtrip [--format text|binary] --out <program-graph-artifact> [graph-input]",
    "zero --out",
    "use zero view --out <file.0> for canonical source, or choose a graph subcommand with command-specific output",
  };
  const ZProgramGraphCommandKind *item = graph_kind(kind);
  return item ? item->out_contract : fallback;
}

void z_program_graph_print_command_help(void) {
  printf("Usage: zero init [--template cli|lib|package] [project-path]; zero query|view|diff|dump|inspect|validate|source-map|roundtrip [--json] [graph-input]; zero status|verify-projection|import|export|merge [--json] [project|zero.toml|zero.json|file.0]\n\n");
  printf("Graph-first project usage: zero init [--json] [--manifest toml|json] [--format text|binary] [--template cli|lib|package] [project-path]\n");
  printf("Output usage: zero dump|validate|roundtrip [--json] [--format text|binary] --out <program-graph-artifact> [graph-input]; zero import [--json] [--format text|binary] --out <program-graph-artifact> [project|zero.toml|zero.json|file.0]\n");
  printf("View output usage: zero view [--json] [--fn <name>] [--out <file.0>] [graph-input]\n");
  printf("Diff textconv usage: zero diff [graph-input]\n");
  printf("Source map usage: zero source-map [--json] [graph-input]\n");
  printf("Query usage: zero query [--json] [--fn <name>] [--find <text>] [--refs <name>] [--calls <name>] [--node <id>] [--depth <n>] [--full] [graph-input|name]\n");
  printf("Reconcile usage: zero reconcile [--json] <base-graph-input> --source <edited-file.0|project|zero.toml|zero.json>\n");
  printf("Repository projection usage: zero status|verify-projection [--json] [project|zero.toml|zero.json|file.0]; zero import [--json] [--format text|binary] [project|zero.toml|zero.json|file.0]; zero export [--json] [project|zero.toml|zero.json|file.0]; zero merge --base <base-zero.graph> --left <left-zero.graph> --right <right-zero.graph> [--json] [project|zero.toml|zero.json|file.0]\n");
  printf("Size usage: zero size [--json] [--target <target>] [--out <artifact>] [graph-input]\n");
  printf("Patch usage: zero patch [--json] [--check-only|--dry-run] [--format text|binary] [--out <program-graph-artifact>] [graph-input] (<patch-file>|--op <operation>)\n");
  printf("  In a graph-first package, zero patch --op <operation> defaults to the current directory.\nPatch operation help: zero patch --op help\n\n");
  printf("Build usage: zero build [--json] [--emit exe|obj|llvm-ir] [--backend direct|llvm|<direct-emitter>] [--target <target>] [--profile debug|dev|release-fast|release-small|tiny|audit] [--release <profile>] [--out <file>] [graph-input]\n\n");
  printf("Run usage: zero run [--target <host-target>] [--profile debug|dev|release-fast|release-small|tiny|audit] [--release <profile>] [--out <file>] [graph-input] [-- args...]\n\n");
  printf("Test usage: zero test [--json] [--filter <name>] [--target <target>] [graph-input]\n\n");
  printf("Inspect modules, symbols, capabilities, static metadata, stdlib helpers, or deterministic ProgramGraph inputs.\n\n");
  printf("Graph commands:\n");
  printf("  init      create a graph-first package with zero.graph as compiler input\n");
  printf("  dump      print or write only the deterministic ProgramGraph\n");
  printf("  import    import source projections into zero.graph, or convert source into a standalone ProgramGraph artifact\n");
  printf("  export    export zero.graph into human-readable .0 source projections\n");
  printf("  query     report compact module, function, body, and patch facts for agents\n");
  printf("  inspect   report semantic graph and compiler facts\n");
  printf("  validate  read ProgramGraph input and optionally write its normalized artifact form\n");
  printf("  view      render ProgramGraph input as a generated Zero view\n");
  printf("  diff      print canonical review text for Git textconv diff drivers\n");
  printf("  source-map map graph nodes to source ranges and semantic identity facts\n");
  printf("  reconcile compare a prior graph with edited source and report identity decisions\n");
  printf("  status    report repository graph projection state without writing files\n");
  printf("  verify-projection check checked-in projection drift without writing files\n");
  printf("  merge     combine independent repository graph store edits by durable node id\n");
  printf("  roundtrip compare graph semantics after direct ProgramGraph lowering\n");
  printf("\nRepository store encoding:\n");
  printf("  binary is the default zero.graph encoding\n");
  printf("  --format text writes readable repository stores when explicitly requested\n");
  printf("  --format binary opts explicit graph artifact outputs into binary storage\n");
  printf("  reads auto-detect text and binary stores and graph artifacts\n");
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
