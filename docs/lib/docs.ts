import type { Doc, DocsGroup } from "./types";

export const docs: Doc[] = [
  {
    slug: "getting-started",
    title: "Getting Started",
    description: "Install Zero and ask an agent to create a graph-first program.",
    path: "/getting-started",
    sourcePath: "/articles/getting-started.md",
    section: "Learn",
  },
  {
    slug: "install",
    title: "Install Guide",
    description: "Install the latest compiler release and validate your environment.",
    path: "/install",
    sourcePath: "/articles/install.md",
    section: "Learn",
  },
  {
    slug: "learn-zero",
    title: "Learn Zero",
    description: "A graph-first language tour using runnable graph examples and projection snippets.",
    path: "/learn",
    sourcePath: "/articles/learn-zero.md",
    section: "Learn",
  },
  {
    slug: "language-reference",
    title: "Language Reference",
    description: "Projection syntax, graph program model, types, control flow, packages, stdlib, and tooling.",
    path: "/reference",
    sourcePath: "/articles/language-reference.md",
    section: "Reference",
  },
  {
    slug: "standard-library",
    title: "Standard Library Reference",
    description: "Graph-backed modules, allocation behavior, capabilities, and helper metadata.",
    path: "/standard-library",
    sourcePath: "/articles/standard-library.md",
    section: "Reference",
  },
  {
    slug: "cli-reference",
    title: "CLI Reference",
    description: "Graph commands, flags, JSON modes, and project workflows.",
    path: "/cli",
    sourcePath: "/articles/cli-reference.md",
    section: "Reference",
  },
  {
    slug: "testing",
    title: "Testing And Reliability",
    description: "Graph-backed zero test workflows, package tests, snapshots, fuzzing, and hardening gates.",
    path: "/testing",
    sourcePath: "/articles/testing.md",
    section: "Reference",
  },
  {
    slug: "optimization",
    title: "Optimization And Size Profiles",
    description: "Profile contracts, size breakdowns, retention reasons, optimization hints, and benchmark trends.",
    path: "/optimization",
    sourcePath: "/articles/optimization.md",
    section: "Reference",
  },
  {
    slug: "package-manifest",
    title: "Package And Manifest Reference",
    description: "zero.toml package manifests, imports, targets, dependencies, and profiles.",
    path: "/package-manifest",
    sourcePath: "/articles/package-manifest.md",
    section: "Reference",
  },
  {
    slug: "cross-compilation",
    title: "Cross-Compilation Guide",
    description: "Targets, capability denial, direct artifacts, and target facts.",
    path: "/cross-compilation",
    sourcePath: "/articles/cross-compilation.md",
    section: "Reference",
  },
  {
    slug: "c-interop",
    title: "C Interop Guide",
    description: "Graph-backed C ABI export support and target library audit facts.",
    path: "/c-interop",
    sourcePath: "/articles/c-interop.md",
    section: "Reference",
  },
  {
    slug: "diagnostics",
    title: "Diagnostics",
    description: "How to read compiler errors and use structured fix plans.",
    path: "/diagnostics",
    sourcePath: "/articles/diagnostics.md",
    section: "Reference",
  },
  {
    slug: "building-from-source",
    title: "Building From Source",
    description: "Build, use, and validate the local compiler.",
    path: "/native-compiler",
    sourcePath: "/articles/building-from-source.md",
    section: "Reference",
  },
  {
    slug: "target-capabilities",
    title: "Target Capabilities",
    description: "Current host and target-neutral capability boundaries.",
    path: "/target-capabilities",
    sourcePath: "/articles/target-capabilities.md",
    section: "Reference",
  },
  {
    slug: "benchmarks",
    title: "Benchmarks",
    description: "Benchmark methodology, cases, and metrics.",
    path: "/benchmarks",
    sourcePath: "/articles/benchmarks.md",
    section: "Reference",
  },
  {
    slug: "examples",
    title: "Examples",
    description: "Runnable graph examples in learning order with copyable commands.",
    path: "/examples",
    sourcePath: "/articles/examples.md",
    section: "Learn",
  },
  {
    slug: "primitives",
    title: "Primitives",
    description: "Graph-visible language and type primitives for values, memory views, ownership, layout, and absence.",
    path: "/primitives",
    sourcePath: "/articles/primitives.md",
    section: "Reference",
  },
  {
    slug: "module-ascii",
    title: "std.ascii",
    description: "ASCII byte predicates, case conversion, and digit value helpers.",
    path: "/modules/ascii",
    sourcePath: "/articles/modules/ascii.md",
    section: "Modules",
  },
  {
    slug: "module-parse",
    title: "std.parse",
    description: "Allocation-free byte scanners and integer/bool parsers.",
    path: "/modules/parse",
    sourcePath: "/articles/modules/parse.md",
    section: "Modules",
  },
  {
    slug: "module-codec",
    title: "std.codec",
    description: "Little-endian integer helpers, unsigned varints, and CRC-32 primitives.",
    path: "/modules/codec",
    sourcePath: "/articles/modules/codec.md",
    section: "Modules",
  },
  {
    slug: "module-mem",
    title: "std.mem",
    description: "Span metadata, copy and equality helpers, and the allocator surface.",
    path: "/modules/mem",
    sourcePath: "/articles/modules/mem.md",
    section: "Modules",
  },
  {
    slug: "module-collections",
    title: "std.collections",
    description: "Fixed-capacity collection operations over caller-owned storage.",
    path: "/modules/collections",
    sourcePath: "/articles/modules/collections.md",
    section: "Modules",
  },
  {
    slug: "module-search",
    title: "std.search",
    description: "Scalar span search and binary-search helpers.",
    path: "/modules/search",
    sourcePath: "/articles/modules/search.md",
    section: "Modules",
  },
  {
    slug: "module-sort",
    title: "std.sort",
    description: "In-place sort and sortedness helpers over caller-owned storage.",
    path: "/modules/sort",
    sourcePath: "/articles/modules/sort.md",
    section: "Modules",
  },
  {
    slug: "module-args",
    title: "std.args",
    description: "Process argument count and indexed lookup for hosted command-line programs.",
    path: "/modules/args",
    sourcePath: "/articles/modules/args.md",
    section: "Modules",
  },
  {
    slug: "module-cli",
    title: "std.cli",
    description: "Hosted flag and option helpers for command-line programs.",
    path: "/modules/cli",
    sourcePath: "/articles/modules/cli.md",
    section: "Modules",
  },
  {
    slug: "module-fmt",
    title: "std.fmt",
    description: "Caller-buffer formatting for booleans and integer text.",
    path: "/modules/fmt",
    sourcePath: "/articles/modules/fmt.md",
    section: "Modules",
  },
  {
    slug: "module-math",
    title: "std.math",
    description: "Pure fixed-width integer helpers and small number-theory routines.",
    path: "/modules/math",
    sourcePath: "/articles/modules/math.md",
    section: "Modules",
  },
  {
    slug: "module-path",
    title: "std.path",
    description: "Fixed-buffer path helpers with explicit storage and target-aware limits.",
    path: "/modules/path",
    sourcePath: "/articles/modules/path.md",
    section: "Modules",
  },
  {
    slug: "module-str",
    title: "std.str",
    description: "Allocation-free byte-string helpers over spans and caller-owned storage.",
    path: "/modules/str",
    sourcePath: "/articles/modules/str.md",
    section: "Modules",
  },
  {
    slug: "module-testing",
    title: "std.testing",
    description: "Bool-returning helpers for test blocks and output checks.",
    path: "/modules/testing",
    sourcePath: "/articles/modules/testing.md",
    section: "Modules",
  },
  {
    slug: "module-text",
    title: "std.text",
    description: "ASCII and UTF-8 byte-backed text validation.",
    path: "/modules/text",
    sourcePath: "/articles/modules/text.md",
    section: "Modules",
  },
  {
    slug: "module-io",
    title: "std.io",
    description: "Buffered reader/writer helpers over caller-owned storage.",
    path: "/modules/io",
    sourcePath: "/articles/modules/io.md",
    section: "Modules",
  },
  {
    slug: "module-fs",
    title: "std.fs",
    description: "Hosted file reads, writes, and existence checks for CLI programs.",
    path: "/modules/fs",
    sourcePath: "/articles/modules/fs.md",
    section: "Modules",
  },
  {
    slug: "module-json",
    title: "std.json",
    description: "Validation, field lookup, explicit-allocator parsing, and caller-buffer writing.",
    path: "/modules/json",
    sourcePath: "/articles/modules/json.md",
    section: "Modules",
  },
  {
    slug: "module-toml",
    title: "std.toml",
    description: "TOML validation, shallow field lookup, and typed scalar decode helpers.",
    path: "/modules/toml",
    sourcePath: "/articles/modules/toml.md",
    section: "Modules",
  },
  {
    slug: "module-log",
    title: "std.log",
    description: "Explicit-buffer structured log record formatting.",
    path: "/modules/log",
    sourcePath: "/articles/modules/log.md",
    section: "Modules",
  },
  {
    slug: "module-url",
    title: "std.url",
    description: "Lexical URL splitting, percent/query encoding, and query helpers.",
    path: "/modules/url",
    sourcePath: "/articles/modules/url.md",
    section: "Modules",
  },
  {
    slug: "module-env",
    title: "std.env",
    description: "Hosted environment variable lookup.",
    path: "/modules/env",
    sourcePath: "/articles/modules/env.md",
    section: "Modules",
  },
  {
    slug: "module-time",
    title: "std.time",
    description: "Duration math plus target-gated monotonic and wall-clock helpers.",
    path: "/modules/time",
    sourcePath: "/articles/modules/time.md",
    section: "Modules",
  },
  {
    slug: "module-rand",
    title: "std.rand",
    description: "Explicit deterministic random sources and target entropy helpers.",
    path: "/modules/rand",
    sourcePath: "/articles/modules/rand.md",
    section: "Modules",
  },
  {
    slug: "module-proc",
    title: "std.proc",
    description: "Host process status helpers behind explicit process capability boundaries.",
    path: "/modules/proc",
    sourcePath: "/articles/modules/proc.md",
    section: "Modules",
  },
  {
    slug: "module-crypto",
    title: "std.crypto",
    description: "Hash, keyed hash, constant-time equality, and target entropy helpers.",
    path: "/modules/crypto",
    sourcePath: "/articles/modules/crypto.md",
    section: "Modules",
  },
  {
    slug: "module-net",
    title: "std.net",
    description: "Network capability metadata, address builders, timeouts, and bootstrap handles.",
    path: "/modules/net",
    sourcePath: "/articles/modules/net.md",
    section: "Modules",
  },
  {
    slug: "module-http",
    title: "std.http",
    description: "HTTP envelope writing, request parsing, hosted fetch, and response metadata helpers.",
    path: "/modules/http",
    sourcePath: "/articles/modules/http.md",
    section: "Modules",
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
  return groups;
}

export function getAdjacentDocs(slug: string): { prev: Doc | null; next: Doc | null } {
  const index = docs.findIndex((d) => d.slug === slug);
  if (index === -1) return { prev: null, next: null };
  return {
    prev: index > 0 ? docs[index - 1] : null,
    next: index < docs.length - 1 ? docs[index + 1] : null,
  };
}
