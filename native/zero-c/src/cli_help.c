#include "cli_help.h"

#include "program_graph_command.h"
#include "program_graph_patch.h"
#include "zero.h"

#include <stdio.h>
#include <string.h>

static bool cli_help_arg_is(const char *arg, const char *expected) {
  return strcmp(arg ? arg : "", expected) == 0;
}

static bool cli_help_is_program_graph_root_command(const char *command) {
  static const char *const commands[] = {"init", "dump", "import", "export", "query", "inspect", "validate", "view", "diff", "source-map", "reconcile", "status", "verify-projection", "merge", "roundtrip", "patch"};
  for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
    if (cli_help_arg_is(command, commands[i])) return true;
  }
  return false;
}

void z_cli_print_help(void) {
  printf("zero %s native bootstrap\n\n", ZERO_VERSION);
  fputs("Usage:\n  zero --version [--json]\n  zero skills [list|get] [--json]\n  zero init [--json] [--manifest toml|json] [--format text|binary] [--template cli|lib|package] [project-path]\n  zero check [--json] [--target <target>] [--emit exe|obj|llvm-ir] [graph-input]\n  zero patch [--json] [--check-only|--dry-run] [--format text|binary] [--out <program-graph-artifact>] [graph-input] (<patch-file>|--op <operation>|--replace-fn <name> --body-file <file|->|--replace-in-fn <name> --old <text> --new <text>)\n  zero test [graph-input]\n  zero fmt <file.0|project|zero.toml|zero.json>\n  zero build [--json] [--emit exe|obj|llvm-ir] [--backend direct|llvm|<direct-emitter>] [--target <target>] [--profile debug|dev|release-fast|release-small|tiny|audit] [--release <profile>] [--out <file>] [graph-input]\n  zero run [--backend direct|llvm|<direct-emitter>] [--target <target>] [--profile debug|dev|release-fast|release-small|tiny|audit] [--release <profile>] [--out <file>] [graph-input] [-- args...]\n  zero tokens --json <file.0|project|zero.toml|zero.json>\n  zero parse --json <file.0|project|zero.toml|zero.json>\n  zero query [--json] [--fn <name>] [--find <text>] [--refs <name>] [--calls <name>] [--node <id>] [--depth <n>] [--full] [--handles] [graph-input|name]\n  zero view [--json] [--fn <name> [--around <text>]] [--outline <module-or-file>] [--out <file.0>] [graph-input]\n  zero diff [--fn <name>] [graph-input]\n  zero status|verify-projection [--json] [project|zero.toml|zero.json|file.0|zero.graph]\n  zero import [--json] [--format text|binary] [--out <program-graph-artifact>] [project|zero.toml|zero.json|file.0]\n  zero export [--json] [project|zero.toml|zero.json|file.0]\n  zero dump|validate|roundtrip [--json] [--format text|binary] [--out <program-graph-artifact>] [graph-input]\n  zero source-map [--json] [graph-input]\n  zero reconcile [--json] <base-graph-input> --source <edited-file.0|project|zero.toml|zero.json>\n  zero merge --base <base-zero.graph> --left <left-zero.graph> --right <right-zero.graph> [--json] [--format text|binary] <project|zero.toml|zero.json|file.0>\n  zero doc [--json] [graph-input]\n  zero size [--json] [--out <artifact>] [graph-input]\n  zero mem [--json] [--target <target>] [graph-input]\n  zero dev [--json] [--trace] [graph-input]\n  zero time --json [graph-input]\n  zero abi check|dump [--json] [--target <target>] [graph-input]\n  zero explain [--json] <code>\n  zero fix --plan --json [graph-input]\n  zero doctor [--json]\n  zero clean [--all]\n  zero targets\n\nExamples:\n  zero init\n  zero init --template cli hello\n  zero run examples/add.graph\n  zero build --emit exe examples/hello.graph --out .zero/out/hello\n  zero check --json examples/hello.graph\n  zero build --target linux-musl-x64 examples/memory-package\n", stdout);
}

void z_cli_print_graph_patch_help_text(void) {
  printf("program graph patch operations\n");
  printf("accepted by zero patch --op, --patch-text, and zero-program-graph-patch v1 files\n");
  const char *const *ops = z_program_graph_patch_operation_examples();
  for (size_t i = 0; ops[i]; i++) printf("  %s\n", ops[i]);
  printf("\nA minimal complete patch file looks exactly like this:\n");
  fputs(z_program_graph_patch_minimal_file_example(), stdout);
  printf("\nFor larger graph edits, put these lines in a patch file under /tmp or pass --patch-text.\n");
  printf("\nTo replace one function body without writing a patch file, use --replace-fn with --body-file.\n");
  printf("--body-file - reads the body rows from stdin, so a heredoc does the whole edit in one call:\n");
  printf("  $ zero patch . --replace-fn greet --body-file - <<'EOF'\n");
  printf("  check world.out.write(\"hello agent\\n\")\n");
  printf("  EOF\n");
  printf("The body holds only the new body rows in canonical projection syntax, exactly what\n");
  printf("zero view --fn <name> prints between the signature braces. No header, no end marker.\n");
  printf("Alternative: write the rows to a file and pass its path:\n");
  printf("  $ zero patch . --replace-fn greet --body-file /tmp/greet.body\n");
  printf("\nTo replace text inside one function without retyping the body, use --replace-in-fn (Edit semantics):\n");
  printf("  $ zero patch . --replace-in-fn greet --old 'return 1' --new 'return 2'\n");
  printf("--old must match the body text zero view --fn <name> prints exactly once; a missing or non-unique\n");
  printf("match fails with the occurrence count. Inline --old/--new accept \\n escapes; --old-file/--new-file\n");
  printf("read the text from a file or - (stdin) for multi-line replacements.\n");
}

void z_cli_print_command_help(const char *command) {
  if (cli_help_arg_is(command, "skills")) {
    printf("Usage: zero skills [list|get] [--json]\n\n");
    printf("List and retrieve version-matched skill content for agents.\n\n");
    printf("Subcommands:\n");
    printf("  list                 list available skills (default)\n");
    printf("  get <name> [--full]  print bundled skill content\n");
    printf("  get <name> --topic <section-prefix>  print only the matching sections, e.g. zero skills get stdlib --topic std.time\n");
    printf("  get --all            print every visible skill\n");
  } else if (cli_help_arg_is(command, "doctor")) {
    printf("Usage: zero doctor [--json]\n\n");
    printf("Check host, compiler, target toolchain, and docs/example readiness.\n");
  } else if (cli_help_arg_is(command, "clean")) {
    printf("Usage: zero clean [--all]\n\n");
    printf("Remove generated output while preserving compiler caches by default.\n\n");
    printf("Flags:\n");
    printf("  --all    remove broader .zero generated state while preserving .zero/bin\n");
  } else if (cli_help_arg_is(command, "check")) {
    printf("Usage: zero check [--json] [--target <target>] [--emit exe|obj|llvm-ir] [--backend direct|llvm|<direct-emitter>] [graph-input]\n\n");
    printf("Validate and typecheck graph-backed Zero input without emitting artifacts.\n");
  } else if (cli_help_arg_is(command, "patch")) {
    printf("Usage: zero patch [--json] [--check-only|--dry-run] [--format text|binary] [--out <program-graph-artifact>] [graph-input] (<patch-file>|--op <operation>|--replace-fn <name> --body-file <file|->|--replace-in-fn <name> --old <text> --new <text>)\n\n");
    z_cli_print_graph_patch_help_text();
  } else if (cli_help_arg_is(command, "build")) {
    printf("Usage: zero build [--json] [--emit exe|obj|llvm-ir] [--backend direct|llvm|<direct-emitter>] [--target <target>] [--profile debug|dev|release-fast|release-small|tiny|audit] [--release <profile>] [--out <file>] [graph-input]\n\n");
    printf("Build direct native executable or object artifacts.\n\n");
    printf("Example: zero build --release tiny --emit exe examples/hello.graph --out .zero/out/hello\n");
  } else if (cli_help_arg_is(command, "run")) {
    printf("Usage: zero run [--backend direct|llvm|<direct-emitter>] [--target <target>] [--profile debug|dev|release-fast|release-small|tiny|audit] [--release <profile>] [--out <file>] [graph-input] [-- args...]\n\n");
    printf("Build a host executable with the selected backend and run it. Direct is the default; LLVM is explicit and requires clang. Program stdout and stderr are passed through unchanged.\n\n");
    printf("Example: zero run examples/add.graph\n");
  } else if (cli_help_arg_is(command, "test")) {
    printf("Usage: zero test [--json] [--filter <name>] [--target <target>] [--cc <path>] [--out <file>] [graph-input]\n\n");
    printf("Build and run inline `test` blocks.\n");
  } else if (cli_help_arg_is(command, "fmt")) {
    printf("Usage: zero fmt [--check] <file.0|project|zero.toml|zero.json>\n\n");
    printf("Print deterministic bootstrap formatting for Zero source.\n");
  } else if (cli_help_arg_is(command, "targets")) {
    printf("Usage: zero targets\n\n");
    printf("Print supported target facts as JSON.\n");
  } else if (cli_help_arg_is(command, "tokens")) {
    printf("Usage: zero tokens --json <file.0|project|zero.toml|zero.json>\n\n");
    printf("Emit source token JSON for oracle comparisons.\n");
  } else if (cli_help_arg_is(command, "parse")) {
    printf("Usage: zero parse --json <file.0|project|zero.toml|zero.json>\n\n");
    printf("Emit normalized source parse JSON for oracle comparisons.\n");
  } else if (cli_help_arg_is(command, "abi")) {
    printf("Usage: zero abi check|dump [--json] [--target <target>] [graph-input]\n\n");
    printf("Check ABI-safe declarations or dump target-aware graph layout facts.\n");
  } else if (cli_help_arg_is(command, "query")) {
    printf("Usage: zero query [--json] [--fn <name>] [--find <text>] [--refs <name>] [--calls <name>] [--node <id>] [--depth <n>] [--full] [--handles] [graph-input|name]\n\n");
    printf("Report compact module, function, body, and patch facts for agents.\n\n");
    printf("A bare name argument that is not an existing path runs --find with that name against the current package.\n");
    printf("--node <id> returns a node-scoped report with span, parents, and children; add --depth <n> (default 1) for a deeper child subtree, or --full for the whole-module report.\n");
    printf("--handles adds stmt and param patch handles to function reports; use it when you are about to patch.\n");
    printf("Use zero view --fn <name> when you want one function's source instead of graph facts.\n\n");
    printf("Examples:\n");
    printf("  zero query userTotals                         find nodes named userTotals in the current package\n");
    printf("  zero query --json --find handleLine .         locate node ids for a name, with source spans\n");
    printf("  zero query --fn handleLine --handles .        stmt and param patch handles for one function\n");
    printf("  zero query --json --node '#decl_12ab34cd' --depth 2 .   one node's fields, edges, and grandchildren\n");
  } else if (cli_help_arg_is(command, "reconcile")) {
    printf("Usage: zero reconcile [--json] <base-graph-input> --source <edited-file.0|project|zero.toml|zero.json>\n\n");
    printf("Compare a prior graph with edited source and report durable node identity decisions before importing.\n\n");
    printf("Examples:\n");
    printf("  zero reconcile zero.graph --source src/main.0        report which node ids survive the edit\n");
    printf("  zero reconcile --json . --source src/main.0          machine-readable identity decisions for the package store\n");
    printf("  zero reconcile --json baseline.graph --source .      compare a saved artifact against the edited package\n");
  } else if (cli_help_arg_is(command, "view")) {
    printf("Usage: zero view [--json] [--fn <name> [--around <text>]] [--outline <module-or-file>] [--out <file.0>] [graph-input]\n\n");
    printf("Render ProgramGraph input as a generated Zero view.\n\n");
    printf("--fn <name> prints just that function's canonical source; a missing name fails with close matches.\n");
    printf("--fn <name> --around <text> prints only the enclosing block that contains the text, eliding the rest of the function.\n");
    printf("--outline <module-or-file> prints function signatures with one-line docs and no bodies; pass . for every module.\n\n");
    printf("Examples:\n");
    printf("  zero view --fn handleLine .          print one function's source from the current package\n");
    printf("  zero view --fn handleLine --around limit .   only the enclosing block mentioning limit\n");
    printf("  zero view --outline src/main.0       signatures plus one-line docs, no bodies\n");
    printf("  zero view --out /tmp/main.0 .        write the whole canonical view to a .0 file\n");
  } else if (cli_help_arg_is(command, "diff")) {
    printf("Usage: zero diff [--fn <name>] [graph-input]\n\n");
    printf("Print canonical review text for ProgramGraph input, for humans and Git textconv diff drivers.\n\n");
    printf("--fn <name> prints just that function's canonical source; a missing name fails with close matches.\n\n");
    printf("Examples:\n");
    printf("  zero diff .                          print the current package's canonical review text\n");
    printf("  zero diff --fn handleLine .          print one function's review text\n");
    printf("  zero diff zero.graph                 render the repository graph store for git textconv\n");
  } else if (cli_help_arg_is(command, "graph") || cli_help_is_program_graph_root_command(command)) {
    z_program_graph_print_command_help();
  } else if (cli_help_arg_is(command, "doc")) {
    printf("Usage: zero doc [--json] [--target <target>] [graph-input]\n\n");
    printf("Emit package API documentation facts without emitting artifacts.\n");
  } else if (cli_help_arg_is(command, "size")) {
    printf("Usage: zero size [--json] [--target <target>] [--profile debug|dev|release-fast|release-small|tiny|audit] [--release <profile>] [--out <artifact>] [graph-input]\n\n");
    printf("Report direct IR size, optional artifact bytes, capabilities, and stdlib helper metadata.\n");
  } else if (cli_help_arg_is(command, "mem")) {
    printf("Usage: zero mem [--json] [--target <target>] [graph-input]\n\n");
    printf("Report direct stack, static, heap, buffer, and runtime memory facts.\n");
  } else if (cli_help_arg_is(command, "dev")) {
    printf("Usage: zero dev [--json] [--trace] [--target <target>] [graph-input]\n\n");
    printf("Emit a direct incremental watch plan, interface fingerprints, and affected-test summary.\n");
  } else if (cli_help_arg_is(command, "time")) {
    printf("Usage: zero time --json [--target <target>] [graph-input]\n\n");
    printf("Emit compiler phase, cache, and invalidation timing facts.\n");
  } else if (cli_help_arg_is(command, "explain")) {
    printf("Usage: zero explain [--json] <diagnostic-code>\n\n");
    printf("Explain a diagnostic and its repair metadata.\n");
  } else if (cli_help_arg_is(command, "fix")) {
    printf("Usage: zero fix (--plan|--patch|--apply) --json [--target <target>] [graph-input]\n\n");
    printf("Print repair plans, reviewable patches, or apply behavior-preserving edits for graph-backed inputs.\n");
  } else if (cli_help_arg_is(command, "version") || cli_help_arg_is(command, "--version")) {
    printf("Usage: zero --version [--json]\n\n");
    printf("Print version, commit, host target, compiler backend, and target toolchain availability.\n");
  } else {
    z_cli_print_help();
  }
}
