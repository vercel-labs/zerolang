import { ImageResponse } from "next/og";
import { readFile } from "node:fs/promises";
import { join } from "node:path";

export { getPageTitle } from "@/lib/page-titles";

type FontCache = {
  geistRegular: Buffer;
  geistPixelSquare: Buffer;
};

let fontCache: FontCache | null = null;

async function loadFonts(): Promise<FontCache> {
  if (fontCache) return fontCache;
  const [geistRegular, geistPixelSquare] = await Promise.all([
    readFile(join(process.cwd(), "public/Geist-Regular.ttf")),
    readFile(join(process.cwd(), "public/GeistPixel-Square.ttf")),
  ]);
  fontCache = { geistRegular, geistPixelSquare };
  return fontCache;
}

export async function renderOgImage(title: string): Promise<ImageResponse> {
  const { geistRegular, geistPixelSquare } = await loadFonts();

  return new ImageResponse(
    (
      <div
        style={{
          width: "100%",
          height: "100%",
          display: "flex",
          flexDirection: "column",
          backgroundColor: "black",
          padding: "60px 80px",
        }}
      >
        <div style={{ display: "flex", alignItems: "center", gap: "16px" }}>
          <svg width="36" height="32" viewBox="0 0 76 65" fill="white">
            <path d="M37.5274 0L75.0548 65H0L37.5274 0Z" />
          </svg>
          <span
            style={{
              fontSize: 36,
              color: "#666",
              fontFamily: "Geist",
              fontWeight: 400,
            }}
          >
            /
          </span>
          <span
            style={{
              fontSize: 36,
              fontFamily: "GeistPixelSquare",
              fontWeight: 400,
              color: "white",
            }}
          >
            zero
          </span>
        </div>

        <div
          style={{
            display: "flex",
            flex: 1,
            flexDirection: "column",
            alignItems: "center",
            justifyContent: "center",
          }}
        >
          {title.split("\n").map((line, i) => (
            <span
              key={i}
              style={{
                fontSize: 72,
                fontFamily: "Geist",
                fontWeight: 400,
                color: "white",
                letterSpacing: "-0.02em",
                textAlign: "center",
                lineHeight: 1.2,
              }}
            >
              {line}
            </span>
          ))}
        </div>
      </div>
    ),
    {
      width: 1200,
      height: 630,
      fonts: [
        {
          name: "Geist",
          data: geistRegular,
          style: "normal",
          weight: 400,
        },
        {
          name: "GeistPixelSquare",
          data: geistPixelSquare,
          style: "normal",
          weight: 400,
        },
      ],
    },
  );
}
