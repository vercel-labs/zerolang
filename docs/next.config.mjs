import { fileURLToPath } from "node:url";
import { dirname } from "node:path";

const __dirname = dirname(fileURLToPath(import.meta.url));
const workspaceRoot = dirname(__dirname);

/** @type {import('next').NextConfig} */
const nextConfig = {
  reactStrictMode: true,
  basePath: process.env.NEXT_BASE_PATH || undefined,
  assetPrefix: process.env.NEXT_ASSET_PREFIX || undefined,
  serverExternalPackages: ["bash-tool", "just-bash", "@mongodb-js/zstd"],
  async headers() {
    return [
      {
        source: "/install.sh",
        headers: [
          {
            key: "Content-Type",
            value: "text/x-shellscript; charset=utf-8",
          },
          {
            key: "Cache-Control",
            value: "public, max-age=300",
          },
        ],
      },
    ];
  },
  pageExtensions: ["ts", "tsx", "mdx"],
  turbopack: {
    // pnpm installs Next at the workspace root; Turbopack needs the same root
    // to resolve workspace dependencies when the docs app runs through Turbo.
    root: workspaceRoot,
  },
};

export default nextConfig;
