import type { Doc, DocsGroup } from "./types";

const SECTION_ORDER = [
  "Start Here",
  "Concepts",
  "Reference",
  "Agent Workflow",
  "Language Pieces",
  "Standard Library",
  "Core",
  "Text And Data",
  "Programs",
  "Runtime And Web",
  "Build And Runtime",
];

export const docs: Doc[] = [
  {
    slug: "getting-started",
    title: "Getting Started",
    description: "Install Zerolang and ask an agent to create a graph-first program you can review.",
    path: "/getting-started",
    sourcePath: "/articles/getting-started.md",
    section: "Start Here",
  },
  {
    slug: "install",
    title: "Install",
    description: "Install the latest compiler release and validate your environment.",
    path: "/install",
    sourcePath: "/articles/install.md",
    section: "Start Here",
  },
  {
    slug: "learn-zero",
    title: "Learn Zerolang",
    description: "A human tour of Zerolang's graph-first workflow, projections, and agent conversations.",
    path: "/learn",
    sourcePath: "/articles/learn-zero.md",
    section: "Start Here",
  },
  {
    slug: "examples",
    title: "Examples",
    description: "Runnable graph examples in learning order with copyable commands.",
    path: "/examples",
    sourcePath: "/articles/examples.md",
    section: "Start Here",
  },
  {
    slug: "graph-architecture",
    title: "Graph Architecture",
    description: "How Zerolang makes the semantic graph the program instead of treating text as the primary interface.",
    path: "/concepts/graph-architecture",
    sourcePath: "/articles/concepts/graph-architecture.md",
    section: "Concepts",
  },
  {
    slug: "semantic-vs-text",
    title: "Semantic Graph Vs Text",
    description: "How semantic graph edits differ from source-text edits, and why that matters for agents.",
    path: "/concepts/semantic-vs-text",
    sourcePath: "/articles/concepts/semantic-vs-text.md",
    section: "Concepts",
  },
  {
    slug: "compile-path",
    title: "Compile Path",
    description: "How Zerolang's graph-native compile path compares with traditional parse-first compilers.",
    path: "/concepts/compile-path",
    sourcePath: "/articles/concepts/compile-path.md",
    section: "Concepts",
  },
  {
    slug: "projections",
    title: "Projections And Round Trips",
    description: "How .0 projections support human review, manual edits, import/export, and no silent divergence.",
    path: "/concepts/projections",
    sourcePath: "/articles/concepts/projections.md",
    section: "Concepts",
  },
  {
    slug: "cli-reference",
    title: "CLI Reference",
    description: "Commands for creating, inspecting, patching, validating, running, and building Zero programs.",
    path: "/cli",
    sourcePath: "/articles/cli-reference.md",
    section: "Reference",
  },
  {
    slug: "diagnostics",
    title: "Diagnostics And Repair",
    description: "How humans and agents read errors, structured diagnostics, and repair plans.",
    path: "/diagnostics",
    sourcePath: "/articles/diagnostics.md",
    section: "Agent Workflow",
  },
  {
    slug: "testing",
    title: "Testing And Reliability",
    description: "Graph-backed zero test workflows, package tests, snapshots, fuzzing, and hardening gates.",
    path: "/testing",
    sourcePath: "/articles/testing.md",
    section: "Agent Workflow",
  },
  {
    slug: "primitives",
    title: "Primitives And Types",
    description: "The graph-visible language pieces behind both graph facts and projection syntax.",
    path: "/primitives",
    sourcePath: "/articles/primitives.md",
    section: "Language Pieces",
  },
  {
    slug: "language-reference",
    title: "Language Model Reference",
    description: "Declarations, functions, control flow, capabilities, ownership, packages, and projections.",
    path: "/reference",
    sourcePath: "/articles/language-reference.md",
    section: "Language Pieces",
  },
  {
    slug: "package-manifest",
    title: "Package And Manifest Reference",
    description: "zero.toml package manifests, imports, targets, dependencies, and profiles.",
    path: "/package-manifest",
    sourcePath: "/articles/package-manifest.md",
    section: "Language Pieces",
  },
  {
    slug: "standard-library",
    title: "Standard Library",
    description: "Graph-backed modules, capabilities, allocation behavior, and helper metadata.",
    path: "/standard-library",
    sourcePath: "/articles/standard-library.md",
    section: "Standard Library",
  },
  {
    slug: "target-capabilities",
    title: "Target Capabilities",
    description: "Current host and target-neutral capability boundaries.",
    path: "/target-capabilities",
    sourcePath: "/articles/target-capabilities.md",
    section: "Build And Runtime",
  },
  {
    slug: "cross-compilation",
    title: "Cross-Compilation",
    description: "Targets, capability denial, direct artifacts, and target facts.",
    path: "/cross-compilation",
    sourcePath: "/articles/cross-compilation.md",
    section: "Build And Runtime",
  },
  {
    slug: "optimization",
    title: "Optimization And Size Profiles",
    description: "Profile contracts, size breakdowns, retention reasons, optimization hints, and benchmark trends.",
    path: "/optimization",
    sourcePath: "/articles/optimization.md",
    section: "Build And Runtime",
  },
  {
    slug: "benchmarks",
    title: "Benchmarks",
    description: "Benchmark methodology, cases, and metrics.",
    path: "/benchmarks",
    sourcePath: "/articles/benchmarks.md",
    section: "Build And Runtime",
  },
  {
    slug: "c-interop",
    title: "C Interop",
    description: "Graph-backed C ABI export support and target library audit facts.",
    path: "/c-interop",
    sourcePath: "/articles/c-interop.md",
    section: "Build And Runtime",
  },
  {
    slug: "building-from-source",
    title: "Building From Source",
    description: "Build, use, and validate the local compiler checkout.",
    path: "/native-compiler",
    sourcePath: "/articles/building-from-source.md",
    section: "Build And Runtime",
  },
  {
    slug: "module-ascii",
    title: "std.ascii",
    description: "ASCII byte predicates, case conversion, and digit value helpers.",
    path: "/modules/ascii",
    sourcePath: "/articles/modules/ascii.md",
    section: "Text And Data",
  },
  {
    slug: "module-parse",
    title: "std.parse",
    description: "Allocation-free byte scanners and integer/bool parsers.",
    path: "/modules/parse",
    sourcePath: "/articles/modules/parse.md",
    section: "Text And Data",
  },
  {
    slug: "module-regex",
    title: "std.regex",
    description: "Compile-once regular expression matching for a documented ECMA-262-leaning subset.",
    path: "/modules/regex",
    sourcePath: "/articles/modules/regex.md",
    section: "Text And Data",
  },
  {
    slug: "module-inet",
    title: "std.inet",
    description: "Target-neutral IPv4, IPv6, and RFC 1123 hostname literal validation and parsing.",
    path: "/modules/inet",
    sourcePath: "/articles/modules/inet.md",
    section: "Text And Data",
  },
  {
    slug: "module-codec",
    title: "std.codec",
    description: "Little-endian integer helpers, unsigned varints, and CRC-32 primitives.",
    path: "/modules/codec",
    sourcePath: "/articles/modules/codec.md",
    section: "Text And Data",
  },
  {
    slug: "module-csv",
    title: "std.csv",
    description: "Allocation-free CSV validation, record scanning, field decoding, and small writers.",
    path: "/modules/csv",
    sourcePath: "/articles/modules/csv.md",
    section: "Text And Data",
  },
  {
    slug: "module-mem",
    title: "std.mem",
    description: "Span metadata, copy and equality helpers, and the allocator surface.",
    path: "/modules/mem",
    sourcePath: "/articles/modules/mem.md",
    section: "Core",
  },
  {
    slug: "module-collections",
    title: "std.collections",
    description: "Fixed-capacity collection operations over caller-owned storage.",
    path: "/modules/collections",
    sourcePath: "/articles/modules/collections.md",
    section: "Core",
  },
  {
    slug: "module-search",
    title: "std.search",
    description: "Scalar span search and binary-search helpers.",
    path: "/modules/search",
    sourcePath: "/articles/modules/search.md",
    section: "Core",
  },
  {
    slug: "module-sort",
    title: "std.sort",
    description: "In-place sort and sortedness helpers over caller-owned storage.",
    path: "/modules/sort",
    sourcePath: "/articles/modules/sort.md",
    section: "Core",
  },
  {
    slug: "module-args",
    title: "std.args",
    description: "Process argument count and indexed lookup for hosted command-line programs.",
    path: "/modules/args",
    sourcePath: "/articles/modules/args.md",
    section: "Programs",
  },
  {
    slug: "module-cli",
    title: "std.cli",
    description: "Hosted flag and option helpers for command-line programs.",
    path: "/modules/cli",
    sourcePath: "/articles/modules/cli.md",
    section: "Programs",
  },
  {
    slug: "module-diag",
    title: "std.diag",
    description: "Source offsets, line spans, and diagnostic location formatting.",
    path: "/modules/diag",
    sourcePath: "/articles/modules/diag.md",
    section: "Text And Data",
  },
  {
    slug: "module-fmt",
    title: "std.fmt",
    description: "Caller-buffer formatting for booleans and integer text.",
    path: "/modules/fmt",
    sourcePath: "/articles/modules/fmt.md",
    section: "Text And Data",
  },
  {
    slug: "module-math",
    title: "std.math",
    description: "Pure fixed-width integer helpers and small number-theory routines.",
    path: "/modules/math",
    sourcePath: "/articles/modules/math.md",
    section: "Core",
  },
  {
    slug: "module-path",
    title: "std.path",
    description: "Fixed-buffer path helpers with explicit storage and target-aware limits.",
    path: "/modules/path",
    sourcePath: "/articles/modules/path.md",
    section: "Programs",
  },
  {
    slug: "module-str",
    title: "std.str",
    description: "Allocation-free byte-string helpers over spans and caller-owned storage.",
    path: "/modules/str",
    sourcePath: "/articles/modules/str.md",
    section: "Text And Data",
  },
  {
    slug: "module-testing",
    title: "std.testing",
    description: "Bool-returning helpers for test blocks and output checks.",
    path: "/modules/testing",
    sourcePath: "/articles/modules/testing.md",
    section: "Programs",
  },
  {
    slug: "module-term",
    title: "std.term",
    description: "ANSI terminal sequences, key-byte decoding, hosted terminal metadata, and raw mode.",
    path: "/modules/term",
    sourcePath: "/articles/modules/term.md",
    section: "Programs",
  },
  {
    slug: "module-text",
    title: "std.text",
    description: "ASCII and UTF-8 byte-backed text validation.",
    path: "/modules/text",
    sourcePath: "/articles/modules/text.md",
    section: "Text And Data",
  },

  {
    slug: "module-unicode",
    title: "std.unicode",
    description: "Strict UTF-8 codepoint decode/encode iteration and codepoint-class helpers.",
    path: "/modules/unicode",
    sourcePath: "/articles/modules/unicode.md",
    section: "Text And Data",
  },
  {
    slug: "module-io",
    title: "std.io",
    description: "Buffered reader/writer helpers over caller-owned storage.",
    path: "/modules/io",
    sourcePath: "/articles/modules/io.md",
    section: "Programs",
  },
  {
    slug: "module-fs",
    title: "std.fs",
    description: "Hosted file reads, writes, and existence checks for CLI programs.",
    path: "/modules/fs",
    sourcePath: "/articles/modules/fs.md",
    section: "Programs",
  },
  {
    slug: "module-json",
    title: "std.json",
    description: "Validation, field lookup, explicit-allocator parsing, and caller-buffer writing.",
    path: "/modules/json",
    sourcePath: "/articles/modules/json.md",
    section: "Text And Data",
  },
  {
    slug: "module-toml",
    title: "std.toml",
    description: "TOML validation, shallow field lookup, and typed scalar decode helpers.",
    path: "/modules/toml",
    sourcePath: "/articles/modules/toml.md",
    section: "Text And Data",
  },
  {
    slug: "module-log",
    title: "std.log",
    description: "Explicit-buffer structured log record formatting.",
    path: "/modules/log",
    sourcePath: "/articles/modules/log.md",
    section: "Text And Data",
  },
  {
    slug: "module-url",
    title: "std.url",
    description: "Lexical URL splitting, percent/query encoding, and query helpers.",
    path: "/modules/url",
    sourcePath: "/articles/modules/url.md",
    section: "Text And Data",
  },
  {
    slug: "module-env",
    title: "std.env",
    description: "Hosted environment variable lookup.",
    path: "/modules/env",
    sourcePath: "/articles/modules/env.md",
    section: "Programs",
  },
  {
    slug: "module-time",
    title: "std.time",
    description: "Duration math, hosted sleep, and target-gated monotonic and wall-clock helpers.",
    path: "/modules/time",
    sourcePath: "/articles/modules/time.md",
    section: "Runtime And Web",
  },
  {
    slug: "module-rand",
    title: "std.rand",
    description: "Explicit deterministic random sources and target entropy helpers.",
    path: "/modules/rand",
    sourcePath: "/articles/modules/rand.md",
    section: "Runtime And Web",
  },
  {
    slug: "module-proc",
    title: "std.proc",
    description: "Host process status helpers behind explicit process capability boundaries.",
    path: "/modules/proc",
    sourcePath: "/articles/modules/proc.md",
    section: "Runtime And Web",
  },
  {
    slug: "module-pty",
    title: "std.pty",
    description: "Hosted pseudoterminal child processes for interactive CLIs and terminal programs.",
    path: "/modules/pty",
    sourcePath: "/articles/modules/pty.md",
    section: "Runtime And Web",
  },
  {
    slug: "module-crypto",
    title: "std.crypto",
    description: "Hash, keyed hash, constant-time equality, and target entropy helpers.",
    path: "/modules/crypto",
    sourcePath: "/articles/modules/crypto.md",
    section: "Runtime And Web",
  },
  {
    slug: "module-net",
    title: "std.net",
    description: "Network capability metadata, address builders, timeouts, and bootstrap handles.",
    path: "/modules/net",
    sourcePath: "/articles/modules/net.md",
    section: "Runtime And Web",
  },
  {
    slug: "module-http",
    title: "std.http",
    description: "HTTP envelope writing, request parsing, hosted fetch, and response metadata helpers.",
    path: "/modules/http",
    sourcePath: "/articles/modules/http.md",
    section: "Runtime And Web",
  },
];

export function findDocBySlug(slug: string): Doc | null {
  return docs.find((doc) => doc.slug === slug) ?? null;
}

export function findDocByPath(path: string): Doc | null {
  return docs.find((doc) => doc.path === path) ?? null;
}

export function groupBySection(items: Doc[]): DocsGroup[] {
  const groups: DocsGroup[] = [];
  const seen = new Set<string>();
  for (const item of items) {
    const section = item.section ?? "Other";
    if (!seen.has(section)) {
      seen.add(section);
      groups.push({ section, items: [] });
    }
    const group = groups.find((g) => g.section === section);
    if (group) group.items.push(item);
  }
  return groups.sort((a, b) => {
    const ai = SECTION_ORDER.indexOf(a.section);
    const bi = SECTION_ORDER.indexOf(b.section);
    if (ai === -1 && bi === -1) return 0;
    if (ai === -1) return 1;
    if (bi === -1) return -1;
    return ai - bi;
  });
}

export function getAdjacentDocs(slug: string): { prev: Doc | null; next: Doc | null } {
  const index = docs.findIndex((d) => d.slug === slug);
  if (index === -1) return { prev: null, next: null };
  return {
    prev: index > 0 ? docs[index - 1] : null,
    next: index < docs.length - 1 ? docs[index + 1] : null,
  };
}
