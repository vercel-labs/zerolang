#!/bin/sh
set -eu

repo="${ZERO_REPO:-vercel-labs/zero}"
install_dir="${ZERO_INSTALL_DIR:-${HOME:-}/.zero/bin}"
trust_dir="${ZERO_TRUSTED_KEYS_DIR:-${HOME:-}/.zero/trust}"
version="${ZERO_VERSION:-latest}"
linux_flavor="${ZERO_LINUX_FLAVOR:-musl}"

fail() {
  printf 'zero install: %s\n' "$1" >&2
  exit 1
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || fail "missing required command: $1"
}

if [ -z "$install_dir" ]; then
  fail 'HOME is not set; set ZERO_INSTALL_DIR to choose an install directory'
fi

need_cmd uname
need_cmd mkdir
need_cmd mktemp
need_cmd chmod
need_cmd awk
need_cmd mv
need_cmd rm
need_cmd openssl

os="$(uname -s)"
arch="$(uname -m)"
exe_suffix=""

case "$os" in
  Darwin)
    platform="darwin"
    ;;
  Linux)
    case "$linux_flavor" in
      musl) platform="linux-musl" ;;
      gnu|glibc) platform="linux" ;;
      *) fail "unsupported ZERO_LINUX_FLAVOR: $linux_flavor" ;;
    esac
    ;;
  MINGW*|MSYS*|CYGWIN*|Windows_NT)
    platform="win32"
    exe_suffix=".exe"
    ;;
  *)
    fail "unsupported operating system: $os"
    ;;
esac

case "$arch" in
  arm64|aarch64)
    cpu="arm64"
    ;;
  x86_64|amd64)
    cpu="x64"
    ;;
  *)
    fail "unsupported CPU architecture: $arch"
    ;;
esac

asset="zero-${platform}-${cpu}${exe_suffix}"
bin_name="zero${exe_suffix}"

case "$version" in
  latest)
    base_url="https://github.com/${repo}/releases/latest/download"
    ;;
  v*)
    base_url="https://github.com/${repo}/releases/download/${version}"
    ;;
  *)
    base_url="https://github.com/${repo}/releases/download/v${version}"
    ;;
esac

tmp="$(mktemp -d "${TMPDIR:-/tmp}/zero-install.XXXXXX")"
root_key_path="$tmp/zero-root-key.pem"
cat > "$root_key_path" <<'KEY'
-----BEGIN PUBLIC KEY-----
MCowBQYDK2VwAyEAtSp+tgdRqSEhijE2xmwcwZDt9NRPPtbQK34yhBFDzE4=
-----END PUBLIC KEY-----
KEY
cleanup() {
  rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM

download() {
  url="$1"
  out="$2"
  if command -v curl >/dev/null 2>&1; then
    curl --fail --location --silent --show-error --retry 3 --output "$out" "$url"
  elif command -v wget >/dev/null 2>&1; then
    wget -q -O "$out" "$url"
  else
    fail 'missing downloader: install curl or wget'
  fi
}

verify_checksum() {
  checksums="$1"
  file="$2"
  line="$(awk -v file="$file" '$2 == file { print }' "$checksums")"
  if [ -z "$line" ]; then
    fail "checksum for $file not found in CHECKSUMS.txt"
  fi

  if command -v sha256sum >/dev/null 2>&1; then
    printf '%s\n' "$line" | (cd "$tmp" && sha256sum -c - >/dev/null)
  elif command -v shasum >/dev/null 2>&1; then
    printf '%s\n' "$line" | (cd "$tmp" && shasum -a 256 -c - >/dev/null)
  else
    fail 'missing checksum tool: install sha256sum or shasum'
  fi
}

hex_to_bin() {
  in_path="$1"
  out_path="$2"
  if command -v xxd >/dev/null 2>&1; then
    xxd -r -p "$in_path" > "$out_path"
    return
  fi
  if command -v python3 >/dev/null 2>&1; then
    python3 - <<'PY' "$in_path" "$out_path"
import binascii
import sys

in_path = sys.argv[1]
out_path = sys.argv[2]
with open(in_path, "r", encoding="utf8") as handle:
    text = handle.read().strip()
with open(out_path, "wb") as handle:
    handle.write(binascii.unhexlify(text))
PY
    return
  fi
  fail 'missing hex decoder: install xxd or python3'
}

verify_signature() {
  file="$1"
  sig_hex="$2"
  sig_bin="$3"
  hex_to_bin "$sig_hex" "$sig_bin"
  openssl pkeyutl -verify -pubin -inkey "$root_key_path" -rawin -in "$file" -sigfile "$sig_bin" >/dev/null
}

printf 'Installing Zero from %s...\n' "$base_url"
download "${base_url}/${asset}" "${tmp}/${asset}"
download "${base_url}/CHECKSUMS.txt" "${tmp}/CHECKSUMS.txt"
download "${base_url}/checksums.txt.sig" "${tmp}/checksums.txt.sig"
download "${base_url}/trusted-keys.json" "${tmp}/trusted-keys.json"
download "${base_url}/trusted-keys.sig" "${tmp}/trusted-keys.sig"

verify_signature "${tmp}/trusted-keys.json" "${tmp}/trusted-keys.sig" "${tmp}/trusted-keys.sig.bin"
verify_signature "${tmp}/CHECKSUMS.txt" "${tmp}/checksums.txt.sig" "${tmp}/checksums.txt.sig.bin"
verify_checksum "${tmp}/CHECKSUMS.txt" "$asset"

mkdir -p "$trust_dir"
chmod 755 "$trust_dir"
mv "${tmp}/trusted-keys.json" "${trust_dir}/trusted-keys.json"
mv "${tmp}/trusted-keys.sig" "${trust_dir}/trusted-keys.sig"

mkdir -p "$install_dir"
chmod 755 "${tmp}/${asset}"
mv "${tmp}/${asset}" "${install_dir}/${bin_name}"

printf 'Installed %s\n' "${install_dir}/${bin_name}"

if "${install_dir}/${bin_name}" --version >/dev/null 2>&1; then
  "${install_dir}/${bin_name}" --version
fi

case ":${PATH:-}:" in
  *":${install_dir}:"*) ;;
  *)
    printf '\nAdd Zero to your PATH:\n'
    printf "  export PATH=\"%s:\$PATH\"\n" "$install_dir"
    ;;
esac
