import { getPageTitle, renderOgImage } from "./og-image";

export async function GET() {
  const title = getPageTitle("") ?? "Zero";
  return renderOgImage(title);
}
