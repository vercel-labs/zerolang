export type Doc = {
  slug: string;
  title: string;
  description: string;
  path: `/${string}`;
  sourcePath: `/articles/${string}.md`;
  section: string;
};

export type DocsGroup = {
  section: string;
  items: Doc[];
};

export type Heading = {
  level: 2 | 3 | 4;
  text: string;
  id: string;
};

export type Article = {
  doc: Doc;
  source: string;
};

export type SearchIndexEntry = {
  title: string;
  href: string;
  section: string;
  content: string;
};

export type SearchResult = {
  title: string;
  href: string;
  section: string;
  snippet: string;
};
