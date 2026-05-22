#!/usr/bin/env -S node --experimental-strip-types --disable-warning=ExperimentalWarning
import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { createServer as createHttpServer } from "node:http";
import { createServer as createHttpsServer } from "node:https";
import { createServer as createTcpServer } from "node:net";
import { mkdir, mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const zero = "bin/zero";
const outDir = ".zero/native-test";
const target =
  process.platform === "darwin" && process.arch === "arm64" ? "darwin-arm64" :
  process.platform === "linux" && process.arch === "x64" ? "linux-x64" :
  null;

function zeroArray(count) {
  return `0_u8; ${count}`;
}

async function canLinkCurl() {
  const src = `/tmp/zero-http-curl-link-${process.pid}.c`;
  const exe = `/tmp/zero-http-curl-link-${process.pid}`;
  await writeFile(src, "#include <curl/curl.h>\nint main(void) { return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK ? 0 : 1; }\n");
  try {
    await execFileAsync("cc", [src, "-lcurl", "-o", exe]);
    return true;
  } catch {
    return false;
  } finally {
    await rm(src, { force: true });
    await rm(exe, { force: true });
  }
}

async function canRunOpenSsl() {
  try {
    await execFileAsync("openssl", ["version"]);
    return true;
  } catch {
    return false;
  }
}

async function assertProviderUnavailableRuntime() {
  const src = `/tmp/zero-http-provider-unavailable-${process.pid}.c`;
  const exe = `/tmp/zero-http-provider-unavailable-${process.pid}`;
  await writeFile(src, `#include "zero_runtime.h"

int main(void) {
  const unsigned char request[] = "GET http://127.0.0.1/\\n\\n";
  unsigned char response[64] = {0};
  uint64_t result = zero_http_fetch_result(
    (ZeroByteView){request, sizeof(request) - 1},
    (ZeroMutByteView){response, sizeof(response)},
    10000000
  );
  return zero_http_result_ok(result) == 0 &&
         zero_http_result_status(result) == 0 &&
         zero_http_result_body_len(result) == 0 &&
         zero_http_result_error(result) == ZERO_HTTP_PROVIDER_UNAVAILABLE &&
         zero_http_response_len((ZeroByteView){response, sizeof(response)}) == 0 &&
         zero_http_response_body_offset((ZeroByteView){response, sizeof(response)}) == ZERO_HTTP_RESPONSE_META_BYTES ? 0 : 1;
}
`);
  try {
    await execFileAsync("cc", [
      "-DZERO_RUNTIME_NO_CURL",
      "-Inative/zero-c/include",
      "native/zero-c/runtime/zero_runtime.c",
      "native/zero-c/runtime/zero_http_curl.c",
      src,
      "-o",
      exe,
    ]);
    await execFileAsync(exe, [], { timeout: 5000 });
  } finally {
    await rm(src, { force: true });
    await rm(exe, { force: true });
  }
}

async function createTlsFixture() {
  const dir = await mkdtemp("/tmp/zero-http-tls-");
  const caKey = `${dir}/ca.key`;
  const caCert = `${dir}/ca.pem`;
  const serverKey = `${dir}/server.key`;
  const serverCsr = `${dir}/server.csr`;
  const serverCert = `${dir}/server.pem`;
  const serverConf = `${dir}/server.cnf`;
  await writeFile(serverConf, `[req]
distinguished_name = req_distinguished_name
req_extensions = v3_req
prompt = no
[req_distinguished_name]
CN = localhost
[v3_req]
subjectAltName = @alt_names
[alt_names]
DNS.1 = localhost
IP.1 = 127.0.0.1
`);
  try {
    await execFileAsync("openssl", ["req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1", "-keyout", caKey, "-out", caCert, "-subj", "/CN=Zero Test CA"]);
    await execFileAsync("openssl", ["req", "-newkey", "rsa:2048", "-nodes", "-keyout", serverKey, "-out", serverCsr, "-subj", "/CN=localhost", "-config", serverConf]);
    await execFileAsync("openssl", ["x509", "-req", "-in", serverCsr, "-CA", caCert, "-CAkey", caKey, "-CAcreateserial", "-out", serverCert, "-days", "1", "-sha256", "-extfile", serverConf, "-extensions", "v3_req"]);
    return { dir, caCert, serverKey, serverCert };
  } catch {
    await rm(dir, { recursive: true, force: true });
    return null;
  }
}

function listen(server) {
  return new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen(0, "127.0.0.1", () => {
      server.off("error", reject);
      resolve(server.address().port);
    });
  });
}

function close(server) {
  return new Promise<void>((resolve, reject) => {
    server.close((error) => error ? reject(error) : resolve());
  });
}

async function runExitCode(path, env = {}) {
  try {
    await execFileAsync(path, [], { timeout: 5000, env: { ...process.env, ...env } });
    return 0;
  } catch (error) {
    if (typeof error.code === "number") return error.code;
    throw error;
  }
}

function assertRuntimeReport(report, targetName) {
  assert.equal(report.generatedCBytes, 0);
  assert.equal(report.objectBackend?.linking?.targetLibraries, "zero-runtime,curl");
  assert.equal(report.objectBackend?.linking?.externalToolchain, "cc");
  assert.equal(report.objectBackend?.httpRuntime?.status, "supported");
  assert.equal(report.objectBackend?.httpRuntime?.provider, "curl");
  assert.equal(report.objectBackend?.httpRuntime?.tlsVerification, true);
  assert.equal(report.objectBackend?.httpRuntime?.customCa?.env, "ZERO_HTTP_TEST_CA_BUNDLE");
  assert(report.objectBackend?.linkerPlan?.staticLibraries?.includes("zero_runtime.o"));
  assert(report.objectBackend?.linkerPlan?.staticLibraries?.includes("zero_http_curl.o"));
  assert(report.objectBackend?.linkerPlan?.systemLibraries?.includes("curl"));
  assert.equal(report.objectBackend?.directFacts?.runtimeHelperCount, 2);
  if (targetName === "darwin-arm64") {
    assert.equal(report.objectBackend?.objectEmission?.path, "direct-macho64-object");
  } else {
    assert.equal(report.objectBackend?.objectEmission?.path, "direct-elf64-object");
  }
}

async function buildAndRun(name, source, expectedCode, env = {}) {
  const src = `${outDir}/${name}.0`;
  const exe = `${outDir}/${name}`;
  const jsonPath = `${exe}.json`;
  await writeFile(src, source);
  const build = await execFileAsync(zero, ["build", "--json", "--emit", "exe", "--target", target, src, "--out", exe], { maxBuffer: 1024 * 1024 });
  await writeFile(jsonPath, build.stdout);
  assertRuntimeReport(JSON.parse(build.stdout), target);
  const code = await runExitCode(exe, env);
  assert.equal(code, expectedCode);
}

async function runHttpJsonExample(baseUrl) {
  const exe = `${outDir}/std-http-json-example`;
  const run = await execFileAsync(zero, ["run", "--out", exe, "examples/std-http-json.0", "--", `GET ${baseUrl}/ok\n\n`], { timeout: 5000 });
  assert.equal(run.stdout, "http json ok\n");
}

async function runHttpRequestExample(baseUrl) {
  const exe = `${outDir}/std-http-request-example`;
  const request = `POST ${baseUrl}/echo\ncontent-type: application/json\nx-zero-test: yes\n\n{"ping":1}`;
  const run = await execFileAsync(zero, ["run", "--out", exe, "examples/std-http-request.0", "--", request], { timeout: 5000 });
  assert.equal(run.stdout, "http request ok\n");
}

async function runHttpHeadersExample(baseUrl) {
  const exe = `${outDir}/std-http-headers-example`;
  const run = await execFileAsync(zero, ["run", "--out", exe, "examples/std-http-headers.0", "--", `GET ${baseUrl}/headers\n\n`, "connection"], { timeout: 5000 });
  assert.equal(run.stdout, "http header found\n");
}

function okSource(baseUrl) {
  return `export c fn main i32
  let net std.net.host()
  let client std.http.client net
  mut response [512]u8 [${zeroArray(512)}]
  let result std.http.fetch client (std.mem.span "GET ${baseUrl}/ok\\n\\n") response (std.time.ms 1000)
  let body_offset std.http.responseBodyOffset response
  let body_len std.http.resultBodyLen result
  if == (std.http.resultOk result) false
    ret 99
  if != (std.http.resultStatus result) 200
    ret 99
  if != body_len 8
    ret 99
  if != (std.http.resultError result) (std.http.errorNone())
    ret 99
  if < (std.http.responseLen response) body_len
    ret 99
  if != response[body_offset] 123_u8
    ret 99
  if != response[+ body_offset 1] 34_u8
    ret 99
  if != response[+ body_offset 2] 111_u8
    ret 99
  if != response[+ body_offset 3] 107_u8
    ret 99
  if != response[+ body_offset 4] 34_u8
    ret 99
  if != response[+ body_offset 5] 58_u8
    ret 99
  if != response[+ body_offset 6] 49_u8
    ret 99
  if != response[+ body_offset 7] 125_u8
    ret 99
  ret 8
`;
}

function fetchRequestSource(baseUrl, method, path, body, expectedStatus, expectedBodyLen, expectedCode) {
  return `export c fn main i32
  let net std.net.host()
  let client std.http.client net
  let request std.mem.span "${method} ${baseUrl}${path}\\ncontent-type: application/json\\nx-zero-test: yes\\n\\n${body}"
  mut response [512]u8 [${zeroArray(512)}]
  let result std.http.fetch client request response (std.time.ms 1000)
  let body_offset std.http.responseBodyOffset response
  if == (std.http.resultOk result) false
    ret 99
  if != (std.http.resultStatus result) ${expectedStatus}
    ret 99
  if != (std.http.resultBodyLen result) ${expectedBodyLen}
    ret 99
  if != (std.http.resultError result) (std.http.errorNone())
    ret 99
  if != response[body_offset] 123_u8
    ret 99
  if == response[+ body_offset 2] 0_u8
    ret 99
  ret ${expectedCode}
`;
}

function resultFailureSource(baseUrl, path, responseSize, timeoutMs, expectedStatus, expectedLen, expectedError, expectedCode) {
  return `export c fn main i32
  let net std.net.host()
  let client std.http.client net
  mut response [${responseSize}]u8 [${zeroArray(responseSize)}]
  let result std.http.fetch client (std.mem.span "GET ${baseUrl}${path}\\n\\n") response (std.time.ms ${timeoutMs})
  if != (std.http.resultOk result) false
    ret 99
  if != (std.http.resultStatus result) ${expectedStatus}
    ret 99
  if != (std.http.resultBodyLen result) ${expectedLen}
    ret 99
  if != (std.http.resultError result) (${expectedError})
    ret 99
  ret ${expectedCode}
`;
}

function headersSource(baseUrl) {
  return `export c fn main i32
  let net std.net.host()
  let client std.http.client net
  mut response [512]u8 [${zeroArray(512)}]
  let result std.http.fetch client (std.mem.span "GET ${baseUrl}/ok\\n\\n") response (std.time.ms 1000)
  let reply std.http.headerValue response (std.mem.span "x-zero-reply")
  let reply_offset std.http.headerOffset reply
  if == (std.http.resultOk result) false
    ret 99
  if != (std.http.resultStatus result) 200
    ret 99
  if <= (std.http.responseHeadersLen response) 16
    ret 99
  if != (std.http.resultError result) (std.http.errorNone())
    ret 99
  if == (std.http.headerFound reply) false
    ret 99
  if != (std.http.headerLen reply) 3
    ret 99
  if != response[reply_offset] 121_u8
    ret 99
  if != response[24] 72_u8
    ret 99
  if != response[25] 84_u8
    ret 99
  if != response[26] 84_u8
    ret 99
  if != response[27] 80_u8
    ret 99
  ret 44
`;
}

function interimHeadersSource(baseUrl) {
  return `export c fn main i32
  let net std.net.host()
  let client std.http.client net
  mut response [512]u8 [${zeroArray(512)}]
  let result std.http.fetch client (std.mem.span "GET ${baseUrl}/interim\\n\\n") response (std.time.ms 1000)
  let reply std.http.headerValue response (std.mem.span "x-zero-reply")
  let reply_offset std.http.headerOffset reply
  let body_offset std.http.responseBodyOffset response
  if == (std.http.resultOk result) false
    ret 99
  if != (std.http.resultStatus result) 200
    ret 99
  if != (std.http.resultBodyLen result) 8
    ret 99
  if != (std.http.resultError result) (std.http.errorNone())
    ret 99
  if == (std.http.headerFound reply) false
    ret 99
  if != (std.http.headerLen reply) 5
    ret 99
  if != response[reply_offset] 102_u8
    ret 99
  if != response[33] 50_u8
    ret 99
  if != response[34] 48_u8
    ret 99
  if != response[35] 48_u8
    ret 99
  if != response[body_offset] 123_u8
    ret 99
  ret 46
`;
}

function headSource(baseUrl) {
  return `export c fn main i32
  let net std.net.host()
  let client std.http.client net
  let request std.mem.span "HEAD ${baseUrl}/ok\\nx-zero-test: yes\\n\\n"
  mut response [512]u8 [${zeroArray(512)}]
  let result std.http.fetch client request response (std.time.ms 1000)
  let reply std.http.headerValue response (std.mem.span "x-zero-reply")
  if == (std.http.resultOk result) false
    ret 99
  if != (std.http.resultStatus result) 200
    ret 99
  if != (std.http.resultBodyLen result) 0
    ret 99
  if <= (std.http.responseHeadersLen response) 16
    ret 99
  if != (std.http.resultError result) (std.http.errorNone())
    ret 99
  if == (std.http.headerFound reply) false
    ret 99
  if != (std.http.headerLen reply) 3
    ret 99
  ret 45
`;
}

function jsonResultSource(baseUrl) {
  return `export c fn main i32
  let net std.net.host()
  let client std.http.client net
  mut response [512]u8 [${zeroArray(512)}]
  let result std.http.fetch client (std.mem.span "GET ${baseUrl}/ok\\n\\n") response (std.time.ms 1000)
  let body_len std.http.resultBodyLen result
  let body_offset std.http.responseBodyOffset response
  let bytes response[body_offset..+ body_offset body_len]
  mut arena_buf [16]u8 [${zeroArray(16)}]
  mut arena std.mem.fixedBufAlloc arena_buf
  let parsed std.json.parseBytes arena bytes
  if == (std.http.resultOk result) false
    ret 99
  if != (std.http.resultStatus result) 200
    ret 99
  if != body_len 8
    ret 99
  if == parsed.has false
    ret 99
  if == (std.json.validateBytes bytes) false
    ret 99
  if != (std.json.streamTokensBytes bytes) 3
    ret 99
  ret 37
`;
}

function invalidUrlSource() {
  return `export c fn main i32
  let net std.net.host()
  let client std.http.client net
  mut response [64]u8 [${zeroArray(64)}]
  let result std.http.fetch client (std.mem.span "GET http://\\n\\n") response (std.time.ms 1000)
  if != (std.http.resultOk result) false
    ret 99
  if != (std.http.resultStatus result) 0
    ret 99
  if != (std.http.resultBodyLen result) 0
    ret 99
  if != (std.http.resultError result) (std.http.errorInvalidUrl())
    ret 99
  ret 36
`;
}

function shorthandRejectedSource(baseUrl) {
  return `export c fn main i32
  let net std.net.host()
  let client std.http.client net
  mut response [64]u8 [${zeroArray(64)}]
  let result std.http.fetch client (std.mem.span "${baseUrl}/ok") response (std.time.ms 1000)
  if != (std.http.resultOk result) false
    ret 99
  if != (std.http.resultStatus result) 0
    ret 99
  if != (std.http.resultBodyLen result) 0
    ret 99
  if != (std.http.resultError result) (std.http.errorInvalidRequest())
    ret 99
  ret 48
`;
}

function unterminatedEnvelopeSource(baseUrl) {
  return `export c fn main i32
  let net std.net.host()
  let client std.http.client net
  mut response [64]u8 [${zeroArray(64)}]
  let result std.http.fetch client (std.mem.span "GET ${baseUrl}/ok") response (std.time.ms 1000)
  if != (std.http.resultOk result) false
    ret 99
  if != (std.http.resultStatus result) 0
    ret 99
  if != (std.http.resultBodyLen result) 0
    ret 99
  if != (std.http.resultError result) (std.http.errorInvalidRequest())
    ret 99
  ret 49
`;
}

function invalidRequestSource(baseUrl) {
  return `export c fn main i32
  let net std.net.host()
  let client std.http.client net
  mut response [128]u8 [${zeroArray(128)}]
  let request std.mem.span "POST ${baseUrl}/echo\\nbad-header\\n\\n"
  let result std.http.fetch client request response (std.time.ms 1000)
  if != (std.http.resultOk result) false
    ret 99
  if != (std.http.resultStatus result) 0
    ret 99
  if != (std.http.resultBodyLen result) 0
    ret 99
  if != (std.http.resultError result) (std.http.errorInvalidRequest())
    ret 99
  ret 43
`;
}

if (!target) {
  process.stdout.write("http runtime smoke skipped: unsupported host target\n");
  process.exit(0);
}

if (!(await canLinkCurl())) {
  process.stdout.write("http runtime smoke skipped: cc cannot link libcurl\n");
  process.exit(0);
}

await mkdir(outDir, { recursive: true });
await assertProviderUnavailableRuntime();

function handleRequest(request, response) {
  if (request.url === "/headers") {
    response.sendDate = false;
    response.writeHead(204, { "connection": "close" });
    response.end();
  } else if (request.url === "/ok") {
    response.writeHead(200, { "content-type": "application/json", "x-zero-reply": "yes" });
    response.end("{\"ok\":1}");
  } else if (request.url === "/echo") {
    const chunks = [];
    request.on("data", (chunk) => chunks.push(chunk));
    request.on("end", () => {
      const body = Buffer.concat(chunks).toString("utf8");
      if (request.method === "POST" &&
          request.headers["x-zero-test"] === "yes" &&
          request.headers["content-type"] === "application/json" &&
          body === "{\"ping\":1}") {
        response.writeHead(201, { "content-type": "application/json", "x-zero-reply": "yes" });
        response.end("{\"echo\":1}");
      } else {
        response.writeHead(400, { "content-type": "text/plain" });
        response.end("bad request");
      }
    });
  } else if (request.url === "/replace") {
    const chunks = [];
    request.on("data", (chunk) => chunks.push(chunk));
    request.on("end", () => {
      const body = Buffer.concat(chunks).toString("utf8");
      if (request.method === "PUT" &&
          request.headers["x-zero-test"] === "yes" &&
          request.headers["content-type"] === "application/json" &&
          body === "{\"value\":2}") {
        response.writeHead(202, { "content-type": "application/json", "x-zero-reply": "yes" });
        response.end("{\"put\":1}");
      } else {
        response.writeHead(400, { "content-type": "text/plain" });
        response.end("bad request");
      }
    });
  } else if (request.url === "/missing") {
    response.writeHead(404, { "content-type": "text/plain" });
    response.end("missing");
  } else if (request.url === "/large") {
    response.writeHead(200, { "content-type": "text/plain" });
    response.end("0123456789abcdef0123456789abcdef");
  } else if (request.url === "/slow") {
    setTimeout(() => {
      response.writeHead(200, { "content-type": "text/plain" });
      response.end("slow");
    }, 250);
  } else {
    response.writeHead(500, { "content-type": "text/plain" });
    response.end("unexpected");
  }
}

function createInterimHeaderServer() {
  return createTcpServer((socket) => {
    socket.once("data", () => {
      socket.end([
        "HTTP/1.1 103 Early Hints\r\n",
        "Link: </zero.css>; rel=preload; as=style\r\n",
        "\r\n",
        "HTTP/1.1 200 OK\r\n",
        "Content-Type: application/json\r\n",
        "X-Zero-Reply: final\r\n",
        "Connection: close\r\n",
        "Content-Length: 8\r\n",
        "\r\n",
        "{\"ok\":1}",
      ].join(""));
    });
    socket.on("error", () => {});
  });
}

const server = createHttpServer(handleRequest);
const port = await listen(server);
const baseUrl = `http://127.0.0.1:${port}`;
const interimServer = createInterimHeaderServer();
const interimPort = await listen(interimServer);
const interimBaseUrl = `http://127.0.0.1:${interimPort}`;

try {
  await buildAndRun("http-runtime-ok", okSource(baseUrl), 8);
  await buildAndRun("http-runtime-404", resultFailureSource(baseUrl, "/missing", 512, 1000, 404, 7, "std.http.errorNone()", 32), 32);
  await buildAndRun("http-runtime-overflow", resultFailureSource(baseUrl, "/large", 32, 1000, 200, 0, "std.http.errorTooLarge()", 33), 33);
  await buildAndRun("http-runtime-timeout", resultFailureSource(baseUrl, "/slow", 512, 25, 0, 0, "std.http.errorTimeout()", 34), 34);
  await buildAndRun("http-runtime-post", fetchRequestSource(baseUrl, "POST", "/echo", "{\\\"ping\\\":1}", 201, 10, 42), 42);
  await buildAndRun("http-runtime-put", fetchRequestSource(baseUrl, "PUT", "/replace", "{\\\"value\\\":2}", 202, 9, 47), 47);
  await buildAndRun("http-runtime-headers", headersSource(baseUrl), 44);
  await buildAndRun("http-runtime-interim-headers", interimHeadersSource(interimBaseUrl), 46);
  await buildAndRun("http-runtime-head", headSource(baseUrl), 45);
  await buildAndRun("http-runtime-result-json", jsonResultSource(baseUrl), 37);
  await runHttpJsonExample(baseUrl);
  await runHttpRequestExample(baseUrl);
  await runHttpHeadersExample(baseUrl);
  await buildAndRun("http-runtime-shorthand-rejected", shorthandRejectedSource(baseUrl), 48);
  await buildAndRun("http-runtime-unterminated-envelope", unterminatedEnvelopeSource(baseUrl), 49);
  await buildAndRun("http-runtime-invalid-url", invalidUrlSource(), 36);
  await buildAndRun("http-runtime-invalid-request", invalidRequestSource(baseUrl), 43);
} finally {
  await close(server);
  await close(interimServer);
}

if (await canRunOpenSsl()) {
  const tls = await createTlsFixture();
  if (tls) {
    const httpsServer = createHttpsServer({
      key: await readFile(tls.serverKey),
      cert: await readFile(tls.serverCert),
    }, handleRequest);
    const httpsPort = await listen(httpsServer);
    const httpsBaseUrl = `https://127.0.0.1:${httpsPort}`;
    try {
      await buildAndRun("http-runtime-https-ok", okSource(httpsBaseUrl), 8, { ZERO_HTTP_TEST_CA_BUNDLE: tls.caCert });
      await buildAndRun("http-runtime-https-untrusted", resultFailureSource(httpsBaseUrl, "/ok", 512, 1000, 0, 0, "std.http.errorTls()", 35), 35);
    } finally {
      await close(httpsServer);
      await rm(tls.dir, { recursive: true, force: true });
    }
  } else {
    process.stdout.write("https runtime smoke skipped: openssl fixture generation failed\n");
  }
} else {
  process.stdout.write("https runtime smoke skipped: openssl unavailable\n");
}

const okReport = JSON.parse(await readFile(`${outDir}/http-runtime-ok.json`, "utf8"));
assertRuntimeReport(okReport, target);
process.stdout.write(`http runtime smoke ok (${target})\n`);
