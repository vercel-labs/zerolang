import { execFileSync } from "node:child_process";

function run(command, args) {
  try {
    return execFileSync(command, args, {
      encoding: "utf8",
      stdio: ["ignore", "pipe", "pipe"],
    }).trim();
  } catch (error) {
    const stderr = error.stderr?.toString().trim() ?? "";
    const stdout = error.stdout?.toString().trim() ?? "";
    throw new Error([stdout, stderr].filter(Boolean).join("\n") || `${command} failed`);
  }
}

function which(command) {
  try {
    return run("sh", ["-c", `command -v ${command}`]);
  } catch {
    return "";
  }
}

const rustup = which("rustup");
const cargo = which("cargo");
const rustc = which("rustc");
const errors = [];
const notes = [];

if (!rustup) {
  errors.push("rustup is not on PATH. Install it with `brew install rustup` and add $(brew --prefix rustup)/bin to PATH.");
} else {
  const targets = run(rustup, ["target", "list", "--installed"]);
  if (!targets.split("\n").includes("wasm32-wasip2")) {
    errors.push("wasm32-wasip2 is not installed. Run: rustup target add wasm32-wasip2");
  }
}

if (cargo.includes("/opt/homebrew/bin/cargo") && rustup) {
  notes.push("Homebrew cargo is first on PATH. Put rustup before Homebrew rust so Zed uses the rustup toolchain:");
  notes.push('  export PATH="$(brew --prefix rustup)/bin:$HOME/.cargo/bin:$PATH"');
}

if (rustc) {
  notes.push(`Active rustc: ${run(rustc, ["--version"])} (${rustc})`);
}

if (errors.length > 0) {
  console.error("Zero Zed extension Rust prerequisites are not ready:\n");
  for (const error of errors) {
    console.error(`- ${error}`);
  }
  if (notes.length > 0) {
    console.error("\nNotes:");
    for (const note of notes) {
      console.error(note);
    }
  }
  process.exit(1);
}

console.log("Rust prerequisites look good for building the Zed extension.");
for (const note of notes) {
  console.log(note);
}
