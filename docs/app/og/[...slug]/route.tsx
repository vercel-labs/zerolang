import { NextResponse } from "next/server";
import { getPageTitle, renderOgImage } from "../og-image";

type OgRouteContext = { params: Promise<{ slug: string[] }> };

export async function GET(_request: Request, { params }: OgRouteContext) {
  const { slug } = await params;
  const title = getPageTitle(slug.join("/"));

  if (!title) {
    return NextResponse.json({ error: "Not found" }, { status: 404 });
  }

  return renderOgImage(title);
}
