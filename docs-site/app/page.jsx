import { ArrowRightIcon, LogoIcon } from "@/components/icons";
import { ButtonLink } from "@/components/button";
import { InstallCopy } from "@/components/install-copy";
import { pageMetadata } from "@/lib/page-metadata";

export const metadata = pageMetadata("");

const PILLARS = [
  {
    metric: "Small",
    label: "native artifacts",
    title: "Built for tiny tools",
    description:
      "Zero is designed around static dispatch, explicit capabilities, no mandatory GC, and no hidden runtime tax. Size reports make artifact costs visible.",
  },
  {
    metric: "JSON-native",
    label: "diagnostics & fixes",
    title: "Agent-first tooling",
    description:
      "Structured diagnostics, graph output, size reports, and typed repair metadata are part of the toolchain rather than an afterthought.",
  },
  {
    metric: "Explicit",
    label: "effects & memory",
    title: "Local reasoning first",
    description:
      "Function signatures expose fallibility and capabilities. Allocation is explicit. Target limits are reported before code generation when possible.",
  },
];

const FEATURES = [
  { title: "No mandatory GC or event loop", description: "The language keeps allocation, cleanup, and outside-world access visible in code." },
  { title: "Cross-target checks", description: "The compiler can check target-neutral code for multiple targets and emit direct artifacts for the documented subset." },
  { title: "C boundary support", description: "Zero exposes C ABI exports and target-aware interop metadata for low-level boundaries." },
  { title: "Capability-based I/O", description: "Functions declare what they touch. The compiler rejects unavailable capabilities at compile time, not runtime." },
  { title: "Built for agents", description: "Stable diagnostic codes, machine-readable docs, and fix plans make code easier for humans and agents to repair together." },
  { title: "One small toolchain", description: "Check, build, test, format, inspect, and document projects from one CLI." },
];

const CODE_EXAMPLE = `<span class="hl-keyword">fun</span> <span class="hl-variable">answer</span>() -> <span class="hl-type">i32</span> {
  <span class="hl-keyword">return</span> <span class="hl-number">40</span> + <span class="hl-number">2</span>
}

<span class="hl-keyword">pub</span> <span class="hl-keyword">fun</span> <span class="hl-variable">main</span>(world: <span class="hl-type">World</span>) -> <span class="hl-type">Void</span> <span class="hl-keyword">raises</span> {
  <span class="hl-keyword">if</span> <span class="hl-variable">answer</span>() == <span class="hl-number">42</span> {
    <span class="hl-keyword">check</span> world.out.<span class="hl-variable">write</span>(<span class="hl-string">"math works\\n"</span>)
  }
}`;

const AGENT_EXAMPLE = `<span class="hl-comment">$ zero check --json</span>
{
  <span class="hl-key">"ok"</span>: <span class="hl-boolean">false</span>,
  <span class="hl-key">"diagnostics"</span>: [{
    <span class="hl-key">"code"</span>: <span class="hl-string">"NAM003"</span>,
    <span class="hl-key">"message"</span>: <span class="hl-string">"unknown identifier"</span>,
    <span class="hl-key">"line"</span>: <span class="hl-number">3</span>,
    <span class="hl-key">"repair"</span>: { <span class="hl-key">"id"</span>: <span class="hl-string">"declare-missing-symbol"</span> }
  }]
}`;

function CodeWindow({ title, html }) {
  return (
    <div className="overflow-hidden rounded-lg border border-border bg-code-bg shadow-card">
      <div className="flex items-center gap-3 border-b border-border bg-surface px-4 py-3">
        <div className="flex gap-1.5">
          <span className="h-2.5 w-2.5 rounded-full bg-border" />
          <span className="h-2.5 w-2.5 rounded-full bg-border" />
          <span className="h-2.5 w-2.5 rounded-full bg-border" />
        </div>
        <span className="font-mono text-xs font-medium text-muted">{title}</span>
      </div>
      <pre className="m-0 overflow-x-auto p-6 text-sm leading-7">
        <code className="text-code-fg" dangerouslySetInnerHTML={{ __html: html }} />
      </pre>
    </div>
  );
}

export default function HomePage() {
  return (
    <div className="relative min-h-screen overflow-hidden">
      <main>
        <section className="relative z-10 mx-auto flex max-w-[52rem] flex-col items-center px-6 pb-16 pt-[clamp(4rem,12vh,8rem)] text-center">
          <div className="mb-6 inline-flex items-center rounded-full border border-border bg-surface px-3 py-1 text-[0.8125rem] font-medium text-muted">
            Experimental
          </div>
          <h1 className="m-0 text-[clamp(2rem,5vw,3.75rem)] font-bold leading-[1.15] tracking-[-0.045em]">
            The programming language
            <br />
            for agents
          </h1>
          <p className="mt-6 max-w-[38rem] text-[clamp(1rem,2vw,1.1875rem)] leading-[1.65] text-muted">
            Zero is a systems language designed so humans and AI agents can read,
            repair, inspect, and ship small native programs together. It keeps
            effects explicit, memory predictable, and compiler output structured.
          </p>
          <InstallCopy />
        </section>

        <section className="relative z-10 mx-auto grid w-[min(100%-3rem,var(--container-content))] grid-cols-1 gap-px overflow-hidden rounded-lg border border-border bg-border md:grid-cols-3">
          {PILLARS.map((p) => (
            <div key={p.title} className="flex flex-col gap-3 bg-bg p-8">
              <div className="mb-2 flex flex-col gap-0.5">
                <span className="font-mono text-2xl font-bold tracking-[-0.03em] text-fg">{p.metric}</span>
                <span className="text-xs font-medium uppercase tracking-[0.04em] text-muted">{p.label}</span>
              </div>
              <h3 className="m-0 text-base font-semibold tracking-[-0.01em]">{p.title}</h3>
              <p className="m-0 text-sm leading-relaxed text-muted">{p.description}</p>
            </div>
          ))}
        </section>

        <section className="relative z-10 mx-auto w-[min(100%-3rem,var(--container-content))] border-t border-border py-[clamp(4rem,8vh,6rem)]">
          <div className="grid grid-cols-1 gap-6 lg:grid-cols-2">
            <CodeWindow title="main.0" html={CODE_EXAMPLE} />
            <CodeWindow title="zero check --json" html={AGENT_EXAMPLE} />
          </div>
          <p className="mx-auto mt-4 max-w-[44rem] text-center text-sm leading-relaxed text-muted">
            Humans read the message. Agents read the JSON. The same CLI surfaces
            diagnostics, repair metadata, graph facts, and size reports.
          </p>
        </section>

        <section className="relative z-10 mx-auto w-[min(100%-3rem,var(--container-content))] border-t border-border py-[clamp(4rem,8vh,6rem)]">
          <div className="mx-auto mb-12 max-w-[36rem] text-center">
            <p className="mb-2 text-[0.8125rem] font-semibold uppercase tracking-[0.04em] text-blue">Design</p>
            <h2 className="mb-4 text-[clamp(1.5rem,4vw,2.25rem)] font-bold leading-[1.15] tracking-[-0.035em]">Everything is explicit.</h2>
            <p className="m-0 leading-[1.65] text-muted">
              No hidden allocator. No implicit async. No magic globals.
              If a function touches the outside world, the signature says so.
            </p>
          </div>
          <div className="grid grid-cols-1 gap-px overflow-hidden rounded-lg border border-border bg-border md:grid-cols-2 lg:grid-cols-3">
            {FEATURES.map((f) => (
              <div key={f.title} className="flex flex-col gap-3 bg-bg p-8 transition hover:bg-surface-muted">
                <h3 className="m-0 text-[0.9375rem] font-semibold tracking-[-0.01em]">{f.title}</h3>
                <p className="m-0 text-sm leading-relaxed text-muted">{f.description}</p>
              </div>
            ))}
          </div>
        </section>

        <section className="relative z-10 border-t border-border px-6 py-[clamp(5rem,12vh,8rem)] text-center">
          <h2 className="mb-3 text-[clamp(1.75rem,5vw,2.75rem)] font-bold leading-[1.1] tracking-[-0.04em]">Start with zero.</h2>
          <p className="mx-auto mb-8 max-w-[32rem] text-[1.0625rem] leading-relaxed text-muted">
            Install the compiler, run an example, and try the agent-native
            workflow.
          </p>
          <div className="flex flex-wrap items-center justify-center gap-3">
            <ButtonLink href="/getting-started" variant="primary" size="lg">
              Get started <ArrowRightIcon />
            </ButtonLink>
          </div>
        </section>
      </main>

      <footer className="relative z-10 border-t border-border py-8">
        <div className="mx-auto flex w-[min(100%-3rem,var(--container-content))] flex-col items-center gap-2 text-center md:flex-row md:justify-between md:text-left">
          <div className="flex items-center gap-2 text-sm font-semibold text-fg">
            <LogoIcon width="14" height="12" />
            <span>Zero</span>
          </div>
          <p className="m-0 text-[0.8125rem] text-muted">Tiny binaries. Explicit effects. Agent-native tooling.</p>
        </div>
      </footer>
    </div>
  );
}
