import "./globals.css";
import type { Metadata, Viewport } from "next";
import type { ReactNode } from "react";
import { ThemeProvider } from "@/components/theme-provider";
import { SiteHeader } from "@/components/site-header";
import { DocsChat } from "@/components/docs-chat";
import { getStarCount } from "@/lib/github";

const SITE_URL = process.env.NEXT_PUBLIC_SITE_URL || "https://zerolang.ai";
const SITE_DESCRIPTION =
  "Zero is a pre-1 agent-first language experiment. Expect breaking changes and run it only in safe, non-production environments.";

export const metadata: Metadata = {
  metadataBase: new URL(SITE_URL),
  title: {
    default: "Zero - Agent-first language experiment.",
    template: "%s | Zero",
  },
  description: SITE_DESCRIPTION,
  openGraph: {
    type: "website",
    locale: "en_US",
    url: SITE_URL,
    siteName: "Zero",
    title: "Zero - Agent-first language experiment.",
    description: SITE_DESCRIPTION,
    images: [{ url: "/og", width: 1200, height: 630, alt: "Zero" }],
  },
  twitter: {
    card: "summary_large_image",
    title: "Zero - Agent-first language experiment.",
    description: SITE_DESCRIPTION,
    images: ["/og"],
  },
};

export const viewport: Viewport = {
  width: "device-width",
  initialScale: 1,
};

export default async function RootLayout({ children }: Readonly<{ children: ReactNode }>) {
  const stars = await getStarCount();

  return (
    <html lang="en" suppressHydrationWarning>
      <body className="bg-bg text-fg antialiased">
        <ThemeProvider>
          <SiteHeader stars={stars} />
          {children}
          <DocsChat />
        </ThemeProvider>
      </body>
    </html>
  );
}
