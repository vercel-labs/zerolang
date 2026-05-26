import * as vscode from "vscode";
import { LanguageClient, TransportKind } from "vscode-languageclient/node.js";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import { resolveZeroRoot, zlsScriptPath } from "../../scripts/resolve-zero-root.mjs";

/** @type {LanguageClient | undefined} */
let client;

/**
 * @param {vscode.ExtensionContext} context
 */
export async function activate(context) {
  const configuredRoot = vscode.workspace.getConfiguration("zero").get("zeroRoot");
  const extensionRoot = dirname(fileURLToPath(import.meta.url));
  const searchRoot = typeof configuredRoot === "string" && configuredRoot.length > 0
    ? configuredRoot
    : join(extensionRoot, "..", "..");
  const zeroRoot = await resolveZeroRoot(searchRoot);
  const zls = zlsScriptPath(zeroRoot);

  const serverOptions = {
    run: {
      command: "node",
      args: ["--experimental-strip-types", "--disable-warning=ExperimentalWarning", zls],
      transport: TransportKind.stdio,
      options: {
        env: {
          ...process.env,
          ZERO_ROOT: zeroRoot,
        },
      },
    },
    debug: {
      command: "node",
      args: ["--experimental-strip-types", "--disable-warning=ExperimentalWarning", zls],
      transport: TransportKind.stdio,
      options: {
        env: {
          ...process.env,
          ZERO_ROOT: zeroRoot,
        },
      },
    },
  };

  const clientOptions = {
    documentSelector: [{ language: "zero", scheme: "file" }],
  };

  client = new LanguageClient("zeroLanguageServer", "Zero Language Server", serverOptions, clientOptions);
  context.subscriptions.push(client.start());
}

export async function deactivate() {
  if (!client) {
    return undefined;
  }
  return client.stop();
}
