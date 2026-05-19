import Link from "next/link";
import type { Metadata } from "next";
import { notFound } from "next/navigation";
import type { HTMLAttributes, ReactNode } from "react";
import { MDXRemote } from "next-mdx-remote/rsc";
import rehypePrettyCode from "rehype-pretty-code";
import rehypeSlug from "rehype-slug";
import remarkGfm from "remark-gfm";
import { docs, getAdjacentDocs, groupBySection } from "@/lib/docs";
import { extractHeadings, readArticleByPath } from "@/lib/articles";
import { pageMetadata } from "@/lib/page-metadata";
import { DocsSidebarShell } from "@/components/docs-sidebar";
import { DocsToc } from "@/components/docs-toc";
import { CopyCodeButton } from "@/components/copy-button";
import { HeadingAnchor } from "@/components/heading-anchor";
import { rehypeZeroHighlight } from "@/lib/rehype-zero-highlight";

type DocsPageParams = { slug?: string[] };
type DocsPageProps = { params: Promise<DocsPageParams> };

export function generateStaticParams(): DocsPageParams[] {
  return docs.map((doc) => ({
    slug: doc.path.replace(/^\//, "").split("/"),
  }));
}

export async function generateMetadata({ params }: DocsPageProps): Promise<Metadata> {
  const { slug } = await params;
  return pageMetadata((slug ?? []).join("/"));
}

const rehypePrettyCodeOptions = {
  theme: { light: "github-light", dark: "github-dark" },
  keepBackground: false,
};

type HeadingProps = HTMLAttributes<HTMLHeadingElement> & {
  id?: string;
  children?: ReactNode;
};

function makeHeading(Tag: "h2" | "h3" | "h4") {
  return function Heading({ id, children, ...rest }: HeadingProps) {
    return (
      <Tag id={id} {...rest} className="group">
        {children}
        <HeadingAnchor id={id} />
      </Tag>
    );
  };
}

const mdxComponents = {
  pre: ({ children, ...props }: HTMLAttributes<HTMLPreElement>) => (
    <div data-code-block className="relative">
      <CopyCodeButton />
      <pre {...props}>{children}</pre>
    </div>
  ),
  h2: makeHeading("h2"),
  h3: makeHeading("h3"),
  h4: makeHeading("h4"),
};

export default async function DocsPage({ params }: DocsPageProps) {
  const { slug } = await params;
  const routePath = "/" + (slug ?? []).join("/");
  const result = await readArticleByPath(routePath);
  if (!result) notFound();

  const { doc, source } = result;
  const headings = extractHeadings(source);
  const { prev, next } = getAdjacentDocs(doc.slug);
  const groups = groupBySection(docs);

  return (
    <div className="grid min-h-screen grid-cols-1 md:grid-cols-[15rem_minmax(0,1fr)] lg:grid-cols-[15rem_minmax(0,1fr)_14rem]">
      <DocsSidebarShell groups={groups} activeSlug={doc.slug} currentTitle={doc.title} />

      <main className="mx-auto w-full max-w-[54rem] px-4 pb-[50vh] pt-6 md:px-12">
        <header className="mb-8 border-b border-border pb-6">
          <p className="mb-2 text-[0.8125rem] font-medium tracking-wide text-muted">
            {doc.section ?? "Documentation"}
          </p>
          <h1 className="m-0 text-[clamp(1.75rem,5vw,2.5rem)] font-bold leading-[1.15] tracking-[-0.04em]">
            {doc.title}
          </h1>
          <p className="m-0 mt-3 max-w-[42rem] leading-[1.7] text-muted">{doc.description}</p>
        </header>

        <article className="prose prose-zinc dark:prose-invert max-w-none prose-headings:scroll-mt-20 prose-headings:tracking-[-0.03em] prose-headings:font-semibold prose-a:text-blue prose-a:font-medium prose-a:no-underline prose-a:hover:underline prose-code:font-normal">
          <MDXRemote
            source={source}
            components={mdxComponents}
            options={{
              mdxOptions: {
                format: "md",
                remarkPlugins: [remarkGfm],
                rehypePlugins: [
                  rehypeSlug,
                  [rehypePrettyCode, rehypePrettyCodeOptions],
                  rehypeZeroHighlight,
                ],
              },
              parseFrontmatter: false,
            }}
          />
        </article>

        <nav aria-label="Pagination" className="mt-16 grid grid-cols-1 gap-4 border-t border-border pt-8 md:grid-cols-2">
          {prev ? (
            <Link
              href={prev.path}
              className="flex flex-col gap-1 rounded-lg border border-border p-4 no-underline transition hover:border-muted"
            >
              <span className="text-xs font-medium uppercase tracking-[0.04em] text-muted">Previous</span>
              <span className="text-[0.9375rem] font-semibold text-blue">{prev.title}</span>
            </Link>
          ) : (
            <span />
          )}
          {next ? (
            <Link
              href={next.path}
              className="flex flex-col items-end gap-1 rounded-lg border border-border p-4 text-right no-underline transition hover:border-muted"
            >
              <span className="text-xs font-medium uppercase tracking-[0.04em] text-muted">Next</span>
              <span className="text-[0.9375rem] font-semibold text-blue">{next.title}</span>
            </Link>
          ) : null}
        </nav>
      </main>

      <DocsToc headings={headings} />
    </div>
  );
}
