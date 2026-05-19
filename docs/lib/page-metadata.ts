import type { Metadata } from "next";
import { PAGE_TITLES } from "./page-titles";
import { findDocByPath } from "./docs";

const SITE_NAME = "Zero";
const DESCRIPTION =
  "Zero is a pre-1 agent-first language experiment. Expect breaking changes and run it only in safe, non-production environments.";

export function pageMetadata(slug: string): Metadata {
  const title = PAGE_TITLES[slug];
  if (title === undefined) return {};

  const displayTitle = title.replace(/\n/g, " ");
  const fullTitle = slug === "" ? `${SITE_NAME} | ${displayTitle}` : `${displayTitle} | ${SITE_NAME}`;
  const ogImageUrl = slug ? `/og/${slug}` : "/og";

  const doc = slug ? findDocByPath(`/${slug}`) : null;
  const description = doc?.description ?? DESCRIPTION;

  return {
    title: slug === "" ? fullTitle : displayTitle,
    description,
    openGraph: {
      type: "website",
      locale: "en_US",
      siteName: SITE_NAME,
      title: fullTitle,
      description,
      images: [
        {
          url: ogImageUrl,
          width: 1200,
          height: 630,
          alt: `${displayTitle} — ${SITE_NAME}`,
        },
      ],
    },
    twitter: {
      card: "summary_large_image",
      title: fullTitle,
      description,
      images: [ogImageUrl],
    },
  };
}
