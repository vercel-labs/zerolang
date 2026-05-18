#include "keys.h"
#include "crypto.h"
#include "hex.h"
#include "ledger.h"
#include "process.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if !defined(_WIN32)
#include <unistd.h>
#else
#include <io.h>
#endif

typedef struct {
  char* label;
  char* name;
  char* email;
} KeyIdentity;

static bool trusted_keys_insecure_enabled = false;
static bool trusted_keys_insecure_warned = false;

void z_trusted_keys_insecure_set(bool enabled) {
  trusted_keys_insecure_enabled = enabled;
}

#if defined(ZERO_TEST)
static const unsigned char trusted_root_public_keys[][32] = {
    {0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe, 0xd3, 0xc9, 0x64, 0x07, 0x3a,
     0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6, 0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a}};
#else
static const unsigned char trusted_root_public_keys[][32] = {
    {0xb5, 0x2a, 0x7e, 0xb6, 0x07, 0x51, 0xa9, 0x21, 0x21, 0x8a, 0x31, 0x36, 0xc6, 0x6c, 0x1c, 0xc1,
     0x90, 0xed, 0xf4, 0xd4, 0x4f, 0x3e, 0xd6, 0xd0, 0x2b, 0x7e, 0x32, 0x84, 0x11, 0x43, 0xcc, 0x4e}};
#endif
static const size_t trusted_root_key_count = sizeof(trusted_root_public_keys) / sizeof(trusted_root_public_keys[0]);

static bool keys_diag(ZDiag* diag, const char* path, int code, const char* message, const char* help) {
  diag->code = code;
  diag->path = path ? z_strdup(path) : NULL;
  diag->line = 1;
  diag->column = 1;
  snprintf(diag->message, sizeof(diag->message), "%s", message);
  snprintf(diag->help, sizeof(diag->help), "%s", help ? help : "check key store");
  return false;
}

static int keys_error(const ZDiag* diag, const char* path, bool json) {
  const char* diag_path = diag && diag->path ? diag->path : (path ? path : "keys");
  if (json) {
    ZBuf buf;
    zbuf_init(&buf);
    zbuf_append(&buf, "{\"success\":false,\"code\":");
    zbuf_appendf(&buf, "%d", diag ? diag->code : 9001);
    zbuf_append(&buf, ",\"message\":");
    z_json_string_append(&buf, diag && diag->message[0] ? diag->message : "error");
    zbuf_append(&buf, ",\"path\":");
    z_json_string_append(&buf, diag_path);
    zbuf_append(&buf, ",\"help\":");
    z_json_string_append(&buf, diag && diag->help[0] ? diag->help : "");
    zbuf_append(&buf, "}\n");
    fputs(buf.data, stdout);
    zbuf_free(&buf);
  } else {
    fprintf(stderr, "%s: %s\n", diag_path, diag && diag->message[0] ? diag->message : "error");
    if (diag && diag->help[0]) fprintf(stderr, "  help: %s\n", diag->help);
  }
  return 1;
}

static const char* home_dir_value(void) {
  const char* home = getenv("HOME");
  if (home && home[0]) return home;
#if defined(_WIN32)
  const char* profile = getenv("USERPROFILE");
  if (profile && profile[0]) return profile;
#endif
  return NULL;
}

static char* text_trim_copy(const char* text) {
  const char* start = text ? text : "";
  while (*start && isspace((unsigned char)*start)) start++;
  const char* end = start + strlen(start);
  while (end > start && isspace((unsigned char)end[-1])) end--;
  return z_strndup(start, (size_t)(end - start));
}

static char* keys_env_text(const char* name) {
  const char* value = getenv(name);
  if (!value || !value[0]) return NULL;
  return z_strdup(value);
}

static char* keys_git_text(const char* field) {
  char command[128];
  snprintf(command, sizeof(command), "git config %s 2>/dev/null", field);
  char* line = z_command_first_line(command);
  if (!line || !line[0]) {
    free(line);
    return NULL;
  }
  return line;
}

static bool keys_stdin_tty(void) {
#if defined(_WIN32)
  return _isatty(_fileno(stdin)) != 0;
#else
  return isatty(fileno(stdin)) != 0;
#endif
}

static char* keys_email_prompt(void) {
  if (!keys_stdin_tty()) return NULL;
  printf("email: ");
  fflush(stdout);
  char line[256];
  if (!fgets(line, sizeof(line), stdin)) return NULL;
  char* trim = text_trim_copy(line);
  if (!trim[0]) {
    free(trim);
    return NULL;
  }
  return trim;
}

static const char* keys_label_default(const char* label) {
  return label && label[0] ? label : "default";
}

static void keys_identity_free(KeyIdentity* identity) {
  if (!identity) return;
  free(identity->label);
  free(identity->name);
  free(identity->email);
  identity->label = NULL;
  identity->name = NULL;
  identity->email = NULL;
}

static bool keys_identity_from_env(KeyIdentity* identity, ZDiag* diag) {
  char* email = keys_env_text("ZERO_KEY_EMAIL");
  if (!email) email = keys_git_text("user.email");
  if (!email) email = keys_email_prompt();
  if (!email) {
    return keys_diag(diag, NULL, 9003, "key email is required",
                     "set ZERO_KEY_EMAIL or run `zero keys new --email <addr>`");
  }
  char* name = keys_env_text("ZERO_KEY_NAME");
  if (!name) name = keys_git_text("user.name");
  if (!name) name = z_strdup("");
  identity->label = z_strdup("default");
  identity->name = name;
  identity->email = email;
  return true;
}

static bool keys_identity_from_args(const char* label, const char* name, const char* email, KeyIdentity* identity,
                                    ZDiag* diag) {
  if (!email || !email[0]) {
    return keys_diag(diag, NULL, 9003, "key email is required",
                     "run `zero keys new --email <addr> [--name <name>] [--label <label>]`");
  }
  identity->label = z_strdup(keys_label_default(label));
  identity->name = z_strdup(name ? name : "");
  identity->email = z_strdup(email);
  return true;
}

static char* keys_root_dir(void) {
  const char* override = getenv("ZERO_KEYS_DIR");
  if (override && override[0]) return z_strdup(override);
  const char* home = home_dir_value();
  if (!home) return z_strdup(".zero/keys");
  char* zero_dir = z_path_join(home, ".zero");
  char* keys_dir = z_path_join(zero_dir, "keys");
  free(zero_dir);
  return keys_dir;
}

static char* keys_active_path(const char* root) {
  return z_path_join(root, "active-key.txt");
}

static char* keys_dir_path(const char* root, const char* id_hex) {
  return z_path_join(root, id_hex);
}

static char* keys_public_path(const char* dir) {
  return z_path_join(dir, "public.key");
}

static char* keys_private_path(const char* dir) {
  return z_path_join(dir, "private.key");
}

static char* keys_identity_path(const char* dir) {
  return z_path_join(dir, "identity.txt");
}

static bool keys_mode_set(const char* path, int mode, ZDiag* diag) {
#if defined(_WIN32)
  (void)path;
  (void)mode;
  (void)diag;
  return true;
#else
  if (chmod(path, mode) != 0) {
    return keys_diag(diag, path, 9001, "key file permissions failed", "check key file permissions");
  }
  return true;
#endif
}

static bool keys_file_write(const char* path, const char* text, int mode, ZDiag* diag) {
  if (!z_write_file(path, text, diag)) return false;
  return keys_mode_set(path, mode, diag);
}

static char* keys_read_file(const char* path, ZDiag* diag) {
  char* text = z_read_file(path, diag);
  if (!text && diag && diag->path == path) {
    diag->path = z_strdup(path);
  }
  return text;
}

static char* trusted_keys_dir(void) {
  const char* override = getenv("ZERO_TRUSTED_KEYS_DIR");
  if (override && override[0]) return z_strdup(override);
  const char* home = home_dir_value();
  if (!home) return z_strdup(".zero/trust");
  char* zero_dir = z_path_join(home, ".zero");
  char* trust_dir = z_path_join(zero_dir, "trust");
  free(zero_dir);
  return trust_dir;
}

static char* trusted_keys_path(void) {
  const char* override = getenv("ZERO_TRUSTED_KEYS");
  if (override && override[0]) return z_strdup(override);
  char* dir = trusted_keys_dir();
  char* path = z_path_join(dir, "trusted-keys.json");
  free(dir);
  return path;
}

static char* trusted_keys_sig_path(void) {
  const char* override = getenv("ZERO_TRUSTED_KEYS_SIG");
  if (override && override[0]) return z_strdup(override);
  char* dir = trusted_keys_dir();
  char* path = z_path_join(dir, "trusted-keys.sig");
  free(dir);
  return path;
}

static const char* trusted_keys_url(void) {
  const char* override = getenv("ZERO_TRUSTED_KEYS_URL");
  if (override && override[0]) return override;
  return "https://github.com/vercel-labs/zero/releases/latest/download/trusted-keys.json";
}

static const char* trusted_keys_sig_url(void) {
  const char* override = getenv("ZERO_TRUSTED_KEYS_SIG_URL");
  if (override && override[0]) return override;
  return "https://github.com/vercel-labs/zero/releases/latest/download/trusted-keys.sig";
}

static void trusted_keys_insecure_warn(void) {
  if (trusted_keys_insecure_warned) return;
  trusted_keys_insecure_warned = true;
  fprintf(stderr, "warning: trusted keys signature verification disabled (--insecure)\n");
}

static bool trusted_keys_signature_ok(const char* text, const char* sig_hex, const char* path, ZDiag* diag) {
  if (trusted_keys_insecure_enabled) trusted_keys_insecure_warn();
  unsigned char signature[64];
  bool signature_ok = z_hex_read(sig_hex, 64, signature);
  if (!signature_ok) {
    if (!trusted_keys_insecure_enabled) {
      return keys_diag(diag, path, 9004, "trusted keys signature hex invalid", "check trusted-keys.sig");
    }
    memset(signature, 0, sizeof(signature));
  }
  size_t text_len = strlen(text ? text : "");
  for (size_t i = 0; i < trusted_root_key_count; i++) {
    if (ed25519_verify(signature, (const unsigned char*)(text ? text : ""), text_len, trusted_root_public_keys[i]))
      return true;
  }
  if (trusted_keys_insecure_enabled) return true;
  return keys_diag(diag, path, 9004, "trusted keys signature invalid", "check trusted-keys.sig");
}

static bool trusted_keys_parse_verified(const char* text, const char* sig_hex, const char* path, LedgerKeys* out,
                                        ZDiag* diag) {
  if (!trusted_keys_signature_ok(text, sig_hex, path, diag)) return false;
  const char* schema_path[] = {"schemaVersion"};
  const char* schema_start = NULL;
  const char* schema_end = NULL;
  if (!z_json_span_path_get(text, schema_path, 1, &schema_start, &schema_end)) {
    return keys_diag(diag, path, 9004, "trusted keys schema missing", "check trusted-keys.json");
  }
  long long schema_value = 0;
  if (!z_json_number_from_span(schema_start, schema_end, &schema_value) || schema_value != 1) {
    return keys_diag(diag, path, 9004, "trusted keys schema invalid", "check trusted-keys.json");
  }
  const char* format_path[] = {"format"};
  char* format = z_json_string_path_get(text, format_path, 1);
  if (!format) {
    return keys_diag(diag, path, 9004, "trusted keys format missing", "check trusted-keys.json");
  }
  bool format_ok = strcmp(format, "zero-trusted-keys-v1") == 0;
  free(format);
  if (!format_ok) {
    return keys_diag(diag, path, 9004, "trusted keys format invalid", "check trusted-keys.json");
  }
  const char* keys_path[] = {"keys"};
  const char* keys_start = NULL;
  const char* keys_end = NULL;
  if (!z_json_span_path_get(text, keys_path, 1, &keys_start, &keys_end)) {
    return keys_diag(diag, path, 9004, "trusted keys list missing", "check trusted-keys.json");
  }
  char* keys_text = z_json_span_copy(keys_start, keys_end);
  if (!keys_text) {
    return keys_diag(diag, path, 9004, "trusted keys list invalid", "check trusted-keys.json");
  }
  bool ok = z_ledger_keys_from_json_data(keys_text, path, out, diag);
  free(keys_text);
  return ok;
}

static bool trusted_keys_load_verified(LedgerKeys* out, ZDiag* diag) {
  char* path = trusted_keys_path();
  char* sig_path = trusted_keys_sig_path();
  char* text = keys_read_file(path, diag);
  if (!text) {
    free(path);
    free(sig_path);
    return false;
  }
  char* sig_text = keys_read_file(sig_path, diag);
  if (!sig_text) {
    if (!trusted_keys_insecure_enabled) {
      free(text);
      free(path);
      free(sig_path);
      return false;
    }
    sig_text = z_strdup("");
  }
  char* sig_trim = text_trim_copy(sig_text);
  free(sig_text);
  bool ok = trusted_keys_parse_verified(text, sig_trim, path, out, diag);
  free(sig_trim);
  free(text);
  free(path);
  free(sig_path);
  return ok;
}

bool z_trusted_keys_load_verified(LedgerKeys* out, ZDiag* diag) {
  return trusted_keys_load_verified(out, diag);
}

static const LedgerPublisher* trusted_keys_publisher_find(const LedgerKeys* keys, const char* key_id) {
  for (size_t i = 0; i < keys->publisher_count; i++) {
    if (strcmp(keys->publishers[i].key_id, key_id) == 0) return &keys->publishers[i];
  }
  return NULL;
}

static bool trusted_keys_revoked_has(const LedgerKeys* keys, const char* key_id) {
  for (size_t i = 0; i < keys->revoked_count; i++) {
    if (strcmp(keys->revoked[i].key_id, key_id) == 0) return true;
  }
  return false;
}

static bool trusted_keys_require_key(const LedgerKeys* trusted, const char* key_id, const char* public_hex,
                                     bool allow_revoked, ZDiag* diag) {
  if (!key_id || !key_id[0]) {
    return keys_diag(diag, NULL, 9004, "trusted key missing", "check trusted-keys.json");
  }
  const LedgerPublisher* publisher = trusted_keys_publisher_find(trusted, key_id);
  if (!publisher) {
    return keys_diag(diag, NULL, 9004, "trusted key missing", "add the key to trusted-keys.json");
  }
  if (!allow_revoked && trusted_keys_revoked_has(trusted, key_id)) {
    return keys_diag(diag, NULL, 9004, "trusted key revoked", "remove the key from use");
  }
  if (public_hex && publisher->public_key && strcmp(public_hex, publisher->public_key) != 0) {
    return keys_diag(diag, NULL, 9004, "trusted key mismatch", "check trusted-keys.json");
  }
  return true;
}

bool z_trusted_keys_allow_key(const KeyUse* use, ZDiag* diag) {
  if (!use || !use->id_hex) {
    return keys_diag(diag, NULL, 9004, "trusted key missing", "check trusted-keys.json");
  }
  LedgerKeys trusted = {0};
  if (!trusted_keys_load_verified(&trusted, diag)) return false;
  bool ok = trusted_keys_require_key(&trusted, use->id_hex, use->public_hex, false, diag);
  ledger_keys_free(&trusted);
  return ok;
}

static bool trusted_keys_shell_safe(const char* text) {
  if (!text || !text[0]) return false;
  for (const char* cursor = text; *cursor; cursor++) {
    if (*cursor == '\'' || *cursor == '\n' || *cursor == '\r') return false;
  }
  return true;
}

static bool trusted_keys_download(const char* url, const char* path, ZDiag* diag) {
  if (!trusted_keys_shell_safe(url) || !trusted_keys_shell_safe(path)) {
    return keys_diag(diag, path, 9004, "trusted keys url invalid", "check trusted keys url");
  }
  char command[1024];
  if (z_command_available("curl")) {
    snprintf(command, sizeof(command), "curl --fail --location --silent --show-error --retry 3 --output '%s' '%s'",
             path, url);
  } else if (z_command_available("wget")) {
    snprintf(command, sizeof(command), "wget -q -O '%s' '%s'", path, url);
  } else {
    return keys_diag(diag, path, 9004, "trusted keys download requires curl or wget", "install curl or wget");
  }
  if (!z_command_ok(command)) {
    return keys_diag(diag, path, 9004, "trusted keys download failed", "check trusted keys url");
  }
  return true;
}

static bool trusted_keys_refresh(ZDiag* diag) {
  char* dir = trusted_keys_dir();
  if (!z_dir_make(dir, Z_DIR_MAKE_PARENTS, diag)) {
    free(dir);
    return false;
  }
  char* json_stage = z_path_join(dir, "trusted-keys.json.stage");
  char* sig_stage = z_path_join(dir, "trusted-keys.sig.stage");
  const char* json_url = trusted_keys_url();
  const char* sig_url = trusted_keys_sig_url();
  if (!trusted_keys_download(json_url, json_stage, diag) || !trusted_keys_download(sig_url, sig_stage, diag)) {
    remove(json_stage);
    remove(sig_stage);
    free(json_stage);
    free(sig_stage);
    free(dir);
    return false;
  }
  char* text = keys_read_file(json_stage, diag);
  char* sig_text = text ? keys_read_file(sig_stage, diag) : NULL;
  if (!text || !sig_text) {
    remove(json_stage);
    remove(sig_stage);
    free(text);
    free(sig_text);
    free(json_stage);
    free(sig_stage);
    free(dir);
    return false;
  }
  char* sig_trim = text_trim_copy(sig_text);
  LedgerKeys trusted = {0};
  bool ok = trusted_keys_parse_verified(text, sig_trim, json_stage, &trusted, diag);
  ledger_keys_free(&trusted);
  free(sig_trim);
  free(text);
  free(sig_text);
  if (!ok) {
    remove(json_stage);
    remove(sig_stage);
    free(json_stage);
    free(sig_stage);
    free(dir);
    return false;
  }
  char* json_path = trusted_keys_path();
  char* sig_path = trusted_keys_sig_path();
  if (rename(json_stage, json_path) != 0 || rename(sig_stage, sig_path) != 0) {
    remove(json_stage);
    remove(sig_stage);
    free(json_path);
    free(sig_path);
    free(json_stage);
    free(sig_stage);
    free(dir);
    return keys_diag(diag, dir, 9004, "trusted keys save failed", "check trusted keys path");
  }
  free(json_path);
  free(sig_path);
  free(json_stage);
  free(sig_stage);
  free(dir);
  return true;
}

void z_key_id_from_public(const unsigned char public_key[32], unsigned char key_id[32]) {
  sha256_hash(public_key, 32, key_id);
}

static char* keys_active_id_read(const char* root, ZDiag* diag) {
  char* path = keys_active_path(root);
  if (!z_path_exists(path)) {
    free(path);
    return NULL;
  }
  char* text = keys_read_file(path, diag);
  free(path);
  if (!text) return NULL;
  char* trim = text_trim_copy(text);
  free(text);
  if (!trim[0]) {
    free(trim);
    return NULL;
  }
  return trim;
}

static bool keys_active_id_write(const char* root, const char* id_hex, ZDiag* diag) {
  char* path = keys_active_path(root);
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_appendf(&buf, "%s\n", id_hex ? id_hex : "");
  bool ok = keys_file_write(path, buf.data ? buf.data : "", 0644, diag);
  zbuf_free(&buf);
  free(path);
  return ok;
}

static bool key_line_copy(const char* line, size_t line_len, const char* label, char** out) {
  size_t label_len = strlen(label);
  if (line_len < label_len) return false;
  if (strncmp(line, label, label_len) != 0) return false;
  free(*out);
  *out = z_strndup(line + label_len, line_len - label_len);
  return true;
}

static bool keys_identity_read(const char* path, KeyIdentity* identity, char** key_id_out, ZDiag* diag) {
  char* text = keys_read_file(path, diag);
  if (!text) return false;
  char* label = NULL;
  char* name = NULL;
  char* email = NULL;
  char* key_id = NULL;
  const char* cursor = text;
  while (*cursor) {
    const char* line_end = strchr(cursor, '\n');
    size_t line_len = line_end ? (size_t)(line_end - cursor) : strlen(cursor);
    if (line_len > 0 && cursor[line_len - 1] == '\r') line_len--;
    if (key_line_copy(cursor, line_len, "label=", &label)) {
      cursor = line_end ? line_end + 1 : cursor + line_len;
      continue;
    }
    if (key_line_copy(cursor, line_len, "name=", &name)) {
      cursor = line_end ? line_end + 1 : cursor + line_len;
      continue;
    }
    if (key_line_copy(cursor, line_len, "email=", &email)) {
      cursor = line_end ? line_end + 1 : cursor + line_len;
      continue;
    }
    if (key_line_copy(cursor, line_len, "key_id=", &key_id)) {
      cursor = line_end ? line_end + 1 : cursor + line_len;
      continue;
    }
    cursor = line_end ? line_end + 1 : cursor + line_len;
  }
  free(text);
  if (!email) {
    free(label);
    free(name);
    free(key_id);
    return keys_diag(diag, path, 9001, "identity email missing", "recreate the key identity");
  }
  if (!key_id) {
    free(label);
    free(name);
    free(email);
    return keys_diag(diag, path, 9001, "identity key id missing", "recreate the key identity");
  }
  if (!label) label = z_strdup("default");
  if (!name) name = z_strdup("");
  identity->label = label;
  identity->name = name;
  identity->email = email;
  if (key_id_out)
    *key_id_out = key_id;
  else
    free(key_id);
  return true;
}

static bool keys_identity_write(const char* path, const KeyIdentity* identity, const char* key_id, ZDiag* diag) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_appendf(&buf, "label=%s\n", identity->label ? identity->label : "");
  zbuf_appendf(&buf, "name=%s\n", identity->name ? identity->name : "");
  zbuf_appendf(&buf, "email=%s\n", identity->email ? identity->email : "");
  zbuf_appendf(&buf, "key_id=%s\n", key_id ? key_id : "");
  bool ok = keys_file_write(path, buf.data ? buf.data : "", 0644, diag);
  zbuf_free(&buf);
  return ok;
}

static bool keys_material_read(const char* dir, const char* id_hex, bool require_private, KeyUse* use, ZDiag* diag) {
  char* public_path = keys_public_path(dir);
  char* public_text = keys_read_file(public_path, diag);
  free(public_path);
  if (!public_text) return false;
  char* public_trim = text_trim_copy(public_text);
  free(public_text);
  if (!public_trim[0]) {
    free(public_trim);
    return keys_diag(diag, dir, 9001, "public key is empty", "recreate the key");
  }
  if (!z_hex_read(public_trim, 32, use->public_key)) {
    free(public_trim);
    return keys_diag(diag, dir, 9001, "public key hex invalid", "recreate the key");
  }
  z_key_id_from_public(use->public_key, use->key_id);
  char key_id_hex[65];
  z_hex_write(use->key_id, 32, key_id_hex);
  if (id_hex && strcmp(id_hex, key_id_hex) != 0) {
    free(public_trim);
    return keys_diag(diag, dir, 9001, "public key id mismatch", "recreate the key");
  }
  use->public_hex = public_trim;
  if (!require_private) {
    char* private_path = keys_private_path(dir);
    use->has_private = z_path_exists(private_path);
    free(private_path);
    return true;
  }
  char* private_path = keys_private_path(dir);
  if (!z_path_exists(private_path)) {
    free(private_path);
    return keys_diag(diag, dir, 9001, "private key missing", "select a key with a private key");
  }
  char* private_text = keys_read_file(private_path, diag);
  free(private_path);
  if (!private_text) return false;
  char* private_trim = text_trim_copy(private_text);
  free(private_text);
  if (!private_trim[0]) {
    free(private_trim);
    return keys_diag(diag, dir, 9001, "private key is empty", "recreate the key");
  }
  if (!z_hex_read(private_trim, 64, use->private_key)) {
    free(private_trim);
    return keys_diag(diag, dir, 9001, "private key hex invalid", "recreate the key");
  }
  if (memcmp(use->public_key, use->private_key + 32, 32) != 0) {
    free(private_trim);
    return keys_diag(diag, dir, 9001, "private key mismatch", "recreate the key");
  }
  free(private_trim);
  use->has_private = true;
  return true;
}

static bool keys_use_load(const char* root, const char* id_hex, bool require_private, KeyUse* use, ZDiag* diag) {
  char* dir = keys_dir_path(root, id_hex);
  if (!z_path_exists(dir)) {
    free(dir);
    return keys_diag(diag, root, 9001, "key id not found", "choose a valid key id");
  }
  if (!keys_material_read(dir, id_hex, require_private, use, diag)) {
    free(dir);
    return false;
  }
  char* identity_path = keys_identity_path(dir);
  KeyIdentity identity = {0};
  char* file_key_id = NULL;
  bool ok = keys_identity_read(identity_path, &identity, &file_key_id, diag);
  free(identity_path);
  if (!ok) {
    free(dir);
    keys_identity_free(&identity);
    free(file_key_id);
    return false;
  }
  if (file_key_id && strcmp(file_key_id, id_hex) != 0) {
    free(dir);
    keys_identity_free(&identity);
    free(file_key_id);
    return keys_diag(diag, dir, 9001, "identity key id mismatch", "recreate the key identity");
  }
  use->id_hex = z_strdup(id_hex);
  use->dir = dir;
  use->label = identity.label;
  use->name = identity.name;
  use->email = identity.email;
  free(file_key_id);
  return true;
}

static bool keys_create(const char* root, const KeyIdentity* identity, bool activate, KeyUse* use, ZDiag* diag) {
#if defined(ZERO_TEST)
  const char* seed_hex = getenv("ZERO_KEY_SEED");
  if (seed_hex && seed_hex[0]) {
    unsigned char seed[32];
    if (!z_hex_read(seed_hex, 32, seed)) {
      return keys_diag(diag, NULL, 9001, "key seed hex invalid", "use a 32-byte hex seed");
    }
    if (!ed25519_keypair_from_seed(use->public_key, use->private_key, seed)) {
      return keys_diag(diag, NULL, 9001, "keypair from seed failed", "retry key creation");
    }
  } else {
    if (!ed25519_keypair(use->public_key, use->private_key)) {
      return keys_diag(diag, NULL, 9001, "keypair create failed", "retry key creation");
    }
  }
#else
  if (!ed25519_keypair(use->public_key, use->private_key)) {
    return keys_diag(diag, NULL, 9001, "keypair create failed", "retry key creation");
  }
#endif
  z_key_id_from_public(use->public_key, use->key_id);
  char id_hex[65];
  z_hex_write(use->key_id, 32, id_hex);
  char public_hex[65];
  z_hex_write(use->public_key, 32, public_hex);
  char* dir = keys_dir_path(root, id_hex);
  if (z_path_exists(dir)) {
    free(dir);
    return keys_diag(diag, root, 9001, "key already exists", "choose a different key label");
  }
  if (!z_dir_make(dir, Z_DIR_MAKE_PARENTS, diag)) {
    free(dir);
    return false;
  }
  char* public_path = keys_public_path(dir);
  ZBuf public_buf;
  zbuf_init(&public_buf);
  zbuf_appendf(&public_buf, "%s\n", public_hex);
  bool ok = keys_file_write(public_path, public_buf.data ? public_buf.data : "", 0644, diag);
  zbuf_free(&public_buf);
  free(public_path);
  if (!ok) {
    free(dir);
    return false;
  }
  char private_hex[129];
  z_hex_write(use->private_key, 64, private_hex);
  char* private_path = keys_private_path(dir);
  ZBuf private_buf;
  zbuf_init(&private_buf);
  zbuf_appendf(&private_buf, "%s\n", private_hex);
  ok = keys_file_write(private_path, private_buf.data ? private_buf.data : "", 0600, diag);
  zbuf_free(&private_buf);
  free(private_path);
  if (!ok) {
    free(dir);
    return false;
  }
  char* identity_path = keys_identity_path(dir);
  ok = keys_identity_write(identity_path, identity, id_hex, diag);
  free(identity_path);
  if (!ok) {
    free(dir);
    return false;
  }
  if (activate) {
    if (!keys_active_id_write(root, id_hex, diag)) {
      free(dir);
      return false;
    }
  }
  use->id_hex = z_strdup(id_hex);
  use->public_hex = z_strdup(public_hex);
  use->dir = dir;
  use->label = z_strdup(identity->label ? identity->label : "");
  use->name = z_strdup(identity->name ? identity->name : "");
  use->email = z_strdup(identity->email ? identity->email : "");
  use->created = true;
  use->has_private = true;
  return true;
}

void keys_use_free(KeyUse* use) {
  if (!use) return;
  free(use->id_hex);
  free(use->public_hex);
  free(use->dir);
  free(use->label);
  free(use->name);
  free(use->email);
  use->id_hex = NULL;
  use->public_hex = NULL;
  use->dir = NULL;
  use->label = NULL;
  use->name = NULL;
  use->email = NULL;
  use->created = false;
  use->has_private = false;
}

static bool keys_use_by_ref(const char* root, const char* ref, bool require_private, KeyUse* use, ZDiag* diag) {
  if (!ref || !ref[0]) return keys_diag(diag, root, 9001, "key reference missing", "choose a key id or label");
  unsigned char key_bytes[32];
  bool is_hex = strlen(ref) == 64 && z_hex_read(ref, 32, key_bytes);
  if (is_hex) return keys_use_load(root, ref, require_private, use, diag);
  DIR* dir = opendir(root);
  if (!dir) return keys_diag(diag, root, 9001, "key store missing", "create a key first");
  KeyUse match = {0};
  bool found = false;
  struct dirent* entry = NULL;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
    if (strcmp(entry->d_name, "active-key.txt") == 0) continue;
    char* dir_path = keys_dir_path(root, entry->d_name);
    if (!z_path_exists(dir_path)) {
      free(dir_path);
      continue;
    }
    KeyUse candidate = {0};
    if (!keys_use_load(root, entry->d_name, false, &candidate, diag)) {
      closedir(dir);
      free(dir_path);
      return false;
    }
    if (candidate.label && strcmp(candidate.label, ref) == 0) {
      if (found) {
        keys_use_free(&candidate);
        keys_use_free(&match);
        closedir(dir);
        free(dir_path);
        return keys_diag(diag, root, 9001, "key label is not unique", "use the key id instead");
      }
      if (require_private) {
        if (!candidate.has_private) {
          keys_use_free(&candidate);
          closedir(dir);
          free(dir_path);
          return keys_diag(diag, root, 9001, "private key missing", "select a key with a private key");
        }
        keys_use_free(&candidate);
        if (!keys_use_load(root, entry->d_name, true, &candidate, diag)) {
          closedir(dir);
          free(dir_path);
          return false;
        }
      }
      match = candidate;
      found = true;
    } else {
      keys_use_free(&candidate);
    }
    free(dir_path);
  }
  closedir(dir);
  if (!found) return keys_diag(diag, root, 9001, "key label not found", "choose a key id or label");
  *use = match;
  return true;
}

static bool ledger_keys_add_publisher(LedgerKeys* keys, const KeyUse* use, ZDiag* diag) {
  if (z_ledger_keys_has_publisher(keys, use->id_hex ? use->id_hex : "")) {
    return keys_diag(diag, NULL, 9002, "ledger already has key", "choose another key");
  }
  LedgerPublisher* next = realloc(keys->publishers, (keys->publisher_count + 1) * sizeof(LedgerPublisher));
  if (!next) return keys_diag(diag, NULL, 9002, "ledger add failed", "retry ledger update");
  keys->publishers = next;
  keys->publishers[keys->publisher_count++] =
      (LedgerPublisher){.key_id = z_strdup(use->id_hex ? use->id_hex : ""),
                        .public_key = z_strdup(use->public_hex ? use->public_hex : ""),
                        .label = z_strdup(use->label ? use->label : ""),
                        .name = z_strdup(use->name ? use->name : ""),
                        .email = z_strdup(use->email ? use->email : "")};
  return true;
}

static bool ledger_keys_add_revoked(LedgerKeys* keys, const char* key_id, const char* reason, ZDiag* diag) {
  if (z_ledger_keys_has_revoked(keys, key_id)) {
    return keys_diag(diag, NULL, 9002, "ledger already revoked key", "choose another key");
  }
  LedgerRevoked* next = realloc(keys->revoked, (keys->revoked_count + 1) * sizeof(LedgerRevoked));
  if (!next) return keys_diag(diag, NULL, 9002, "ledger revoke failed", "retry ledger update");
  keys->revoked = next;
  keys->revoked[keys->revoked_count++] =
      (LedgerRevoked){.key_id = z_strdup(key_id ? key_id : ""), .reason = z_strdup(reason ? reason : "")};
  return true;
}

bool keys_active_or_create(KeyUse* out, ZDiag* diag) {
  char* root = keys_root_dir();
  if (!root) return keys_diag(diag, NULL, 9001, "key store root missing", "set ZERO_KEYS_DIR");
  char* active_id = keys_active_id_read(root, diag);
  if (diag->code != 0) {
    free(active_id);
    free(root);
    return false;
  }
  if (active_id) {
    bool ok = keys_use_load(root, active_id, true, out, diag);
    out->created = false;
    free(active_id);
    free(root);
    return ok;
  }
  KeyIdentity identity = {0};
  if (!keys_identity_from_env(&identity, diag)) {
    free(root);
    return false;
  }
  bool ok = keys_create(root, &identity, true, out, diag);
  keys_identity_free(&identity);
  free(root);
  return ok;
}

static bool keys_import(const char* label, const char* name, const char* email, const char* public_hex, bool activate,
                        KeyUse* out, ZDiag* diag) {
  if (!public_hex || !public_hex[0]) {
    return keys_diag(diag, NULL, 9001, "public key is required", "pass --public <hex>");
  }
  KeyIdentity identity = {0};
  if (!keys_identity_from_args(label, name, email, &identity, diag)) return false;
  unsigned char public_key[32];
  if (!z_hex_read(public_hex, 32, public_key)) {
    keys_identity_free(&identity);
    return keys_diag(diag, NULL, 9001, "public key hex invalid", "pass --public <hex>");
  }
  unsigned char key_id[32];
  z_key_id_from_public(public_key, key_id);
  char id_hex[65];
  z_hex_write(key_id, 32, id_hex);
  char* root = keys_root_dir();
  char* dir = keys_dir_path(root, id_hex);
  if (z_path_exists(dir)) {
    free(root);
    free(dir);
    keys_identity_free(&identity);
    return keys_diag(diag, NULL, 9001, "key already exists", "choose another key");
  }
  if (!z_dir_make(dir, Z_DIR_MAKE_PARENTS, diag)) {
    free(root);
    free(dir);
    keys_identity_free(&identity);
    return false;
  }
  char* public_path = keys_public_path(dir);
  ZBuf public_buf;
  zbuf_init(&public_buf);
  zbuf_appendf(&public_buf, "%s\n", public_hex);
  bool ok = keys_file_write(public_path, public_buf.data ? public_buf.data : "", 0644, diag);
  zbuf_free(&public_buf);
  free(public_path);
  if (!ok) {
    free(root);
    free(dir);
    keys_identity_free(&identity);
    return false;
  }
  char* identity_path = keys_identity_path(dir);
  ok = keys_identity_write(identity_path, &identity, id_hex, diag);
  free(identity_path);
  if (!ok) {
    free(root);
    free(dir);
    keys_identity_free(&identity);
    return false;
  }
  if (activate) {
    if (!keys_active_id_write(root, id_hex, diag)) {
      free(root);
      free(dir);
      keys_identity_free(&identity);
      return false;
    }
  }
  if (out) {
    memset(out, 0, sizeof(*out));
    memcpy(out->public_key, public_key, 32);
    memcpy(out->key_id, key_id, 32);
    out->id_hex = z_strdup(id_hex);
    out->public_hex = z_strdup(public_hex);
    out->dir = dir;
    out->label = z_strdup(identity.label ? identity.label : "");
    out->name = z_strdup(identity.name ? identity.name : "");
    out->email = z_strdup(identity.email ? identity.email : "");
    out->created = true;
    out->has_private = false;
  } else {
    free(dir);
  }
  keys_identity_free(&identity);
  free(root);
  return true;
}

static void keys_list_free(KeyUse* items, size_t count) {
  for (size_t i = 0; i < count; i++) keys_use_free(&items[i]);
  free(items);
}

static bool keys_list_load(KeyUse** items_out, size_t* count_out, char** active_out, ZDiag* diag) {
  *items_out = NULL;
  *count_out = 0;
  char* root = keys_root_dir();
  if (!root) return keys_diag(diag, NULL, 9001, "key store root missing", "set ZERO_KEYS_DIR");
  char* active_id = keys_active_id_read(root, diag);
  DIR* dir = opendir(root);
  if (!dir) {
    if (errno == ENOENT) {
      free(root);
      *active_out = active_id;
      return true;
    }
    keys_diag(diag, "keys", 9001, "key store open failed", "check key store path");
    free(root);
    free(active_id);
    return false;
  }
  struct dirent* entry = NULL;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
    if (strcmp(entry->d_name, "active-key.txt") == 0) continue;
    KeyUse use = {0};
    if (!keys_use_load(root, entry->d_name, false, &use, diag)) {
      closedir(dir);
      keys_list_free(*items_out, *count_out);
      free(root);
      free(active_id);
      return false;
    }
    KeyUse* next = realloc(*items_out, (*count_out + 1) * sizeof(KeyUse));
    if (!next) {
      closedir(dir);
      keys_use_free(&use);
      keys_list_free(*items_out, *count_out);
      keys_diag(diag, "keys", 9001, "key list allocation failed", "retry key list");
      free(root);
      free(active_id);
      return false;
    }
    *items_out = next;
    (*items_out)[(*count_out)++] = use;
  }
  closedir(dir);
  free(root);
  *active_out = active_id;
  return true;
}

static void keys_list_print(KeyUse* items, size_t count, const char* active_id) {
  if (count == 0) {
    printf("no keys\n");
    return;
  }
  for (size_t i = 0; i < count; i++) {
    KeyUse* use = &items[i];
    bool active = active_id && strcmp(active_id, use->id_hex) == 0;
    printf("%s %s\n", active ? "*" : "-", use->id_hex ? use->id_hex : "");
    printf("  label: %s\n", use->label ? use->label : "");
    printf("  name: %s\n", use->name ? use->name : "");
    printf("  email: %s\n", use->email ? use->email : "");
    printf("  path: %s\n", use->dir ? use->dir : "");
    printf("  private: %s\n", use->has_private ? "yes" : "no");
  }
}

static void keys_list_print_json(KeyUse* items, size_t count, const char* active_id) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, "{\"success\":true,\"keys\":[");
  for (size_t i = 0; i < count; i++) {
    KeyUse* use = &items[i];
    if (i > 0) zbuf_append(&buf, ",");
    zbuf_append(&buf, "{\"id\":");
    z_json_string_append(&buf, use->id_hex ? use->id_hex : "");
    zbuf_append(&buf, ",\"label\":");
    z_json_string_append(&buf, use->label ? use->label : "");
    zbuf_append(&buf, ",\"name\":");
    z_json_string_append(&buf, use->name ? use->name : "");
    zbuf_append(&buf, ",\"email\":");
    z_json_string_append(&buf, use->email ? use->email : "");
    zbuf_append(&buf, ",\"dir\":");
    z_json_string_append(&buf, use->dir ? use->dir : "");
    zbuf_append(&buf, ",\"active\":");
    zbuf_append(&buf, (active_id && use->id_hex && strcmp(active_id, use->id_hex) == 0) ? "true" : "false");
    zbuf_append(&buf, ",\"private\":");
    zbuf_append(&buf, use->has_private ? "true" : "false");
    zbuf_append(&buf, "}");
  }
  zbuf_append(&buf, "]}\n");
  fputs(buf.data, stdout);
  zbuf_free(&buf);
}

static int keys_list_command(bool json) {
  ZDiag diag = {0};
  KeyUse* items = NULL;
  size_t count = 0;
  char* active_id = NULL;
  if (!keys_list_load(&items, &count, &active_id, &diag)) {
    return keys_error(&diag, "keys", json);
  }
  if (json)
    keys_list_print_json(items, count, active_id);
  else
    keys_list_print(items, count, active_id);
  keys_list_free(items, count);
  free(active_id);
  return 0;
}

static int keys_show_command(const char* ref, bool json) {
  ZDiag diag = {0};
  char* root = keys_root_dir();
  KeyUse use = {0};
  if (!keys_use_by_ref(root, ref, false, &use, &diag)) {
    free(root);
    return keys_error(&diag, "keys", json);
  }
  if (json) {
    ZBuf buf;
    zbuf_init(&buf);
    zbuf_append(&buf, "{\"success\":true,\"key\":{");
    zbuf_append(&buf, "\"id\":");
    z_json_string_append(&buf, use.id_hex ? use.id_hex : "");
    zbuf_append(&buf, ",\"label\":");
    z_json_string_append(&buf, use.label ? use.label : "");
    zbuf_append(&buf, ",\"name\":");
    z_json_string_append(&buf, use.name ? use.name : "");
    zbuf_append(&buf, ",\"email\":");
    z_json_string_append(&buf, use.email ? use.email : "");
    zbuf_append(&buf, ",\"dir\":");
    z_json_string_append(&buf, use.dir ? use.dir : "");
    zbuf_append(&buf, ",\"private\":");
    zbuf_append(&buf, use.has_private ? "true" : "false");
    zbuf_append(&buf, "}}\n");
    fputs(buf.data, stdout);
    zbuf_free(&buf);
  } else {
    printf("id: %s\n", use.id_hex ? use.id_hex : "");
    printf("label: %s\n", use.label ? use.label : "");
    printf("name: %s\n", use.name ? use.name : "");
    printf("email: %s\n", use.email ? use.email : "");
    printf("path: %s\n", use.dir ? use.dir : "");
    printf("private: %s\n", use.has_private ? "yes" : "no");
  }
  keys_use_free(&use);
  free(root);
  return 0;
}

static int keys_use_command(const char* ref, bool json) {
  ZDiag diag = {0};
  char* root = keys_root_dir();
  KeyUse use = {0};
  if (!keys_use_by_ref(root, ref, true, &use, &diag)) {
    free(root);
    return keys_error(&diag, "keys", json);
  }
  bool ok = keys_active_id_write(root, use.id_hex, &diag);
  if (!ok) {
    keys_use_free(&use);
    free(root);
    return keys_error(&diag, "keys", json);
  }
  if (json) {
    ZBuf buf;
    zbuf_init(&buf);
    zbuf_append(&buf, "{\"success\":true,\"active\":");
    z_json_string_append(&buf, use.id_hex ? use.id_hex : "");
    zbuf_append(&buf, "}\n");
    fputs(buf.data, stdout);
    zbuf_free(&buf);
  } else {
    printf("active key: %s\n", use.id_hex ? use.id_hex : "");
  }
  keys_use_free(&use);
  free(root);
  return 0;
}

static int keys_new_command(const char* label, const char* name, const char* email, bool activate, bool json) {
  ZDiag diag = {0};
  KeyIdentity identity = {0};
  if (!keys_identity_from_args(label, name, email, &identity, &diag)) {
    return keys_error(&diag, "keys", json);
  }
  char* root = keys_root_dir();
  KeyUse use = {0};
  bool ok = keys_create(root, &identity, activate, &use, &diag);
  keys_identity_free(&identity);
  free(root);
  if (!ok) {
    return keys_error(&diag, "keys", json);
  }
  if (json) {
    ZBuf buf;
    zbuf_init(&buf);
    zbuf_append(&buf, "{\"success\":true,\"key\":{");
    zbuf_append(&buf, "\"id\":");
    z_json_string_append(&buf, use.id_hex ? use.id_hex : "");
    zbuf_append(&buf, ",\"label\":");
    z_json_string_append(&buf, use.label ? use.label : "");
    zbuf_append(&buf, ",\"name\":");
    z_json_string_append(&buf, use.name ? use.name : "");
    zbuf_append(&buf, ",\"email\":");
    z_json_string_append(&buf, use.email ? use.email : "");
    zbuf_append(&buf, ",\"dir\":");
    z_json_string_append(&buf, use.dir ? use.dir : "");
    zbuf_append(&buf, "},\"active\":");
    zbuf_append(&buf, activate ? "true" : "false");
    zbuf_append(&buf, "}\n");
    fputs(buf.data, stdout);
    zbuf_free(&buf);
  } else {
    printf("created key %s\n", use.id_hex ? use.id_hex : "");
    printf("path: %s\n", use.dir ? use.dir : "");
  }
  keys_use_free(&use);
  return 0;
}

static int keys_import_command(const char* label, const char* name, const char* email, const char* public_hex,
                               bool activate, bool json) {
  ZDiag diag = {0};
  KeyUse use = {0};
  bool ok = keys_import(label, name, email, public_hex, activate, &use, &diag);
  if (!ok) {
    return keys_error(&diag, "keys", json);
  }
  if (json) {
    ZBuf buf;
    zbuf_init(&buf);
    zbuf_append(&buf, "{\"success\":true,\"key\":{");
    zbuf_append(&buf, "\"id\":");
    z_json_string_append(&buf, use.id_hex ? use.id_hex : "");
    zbuf_append(&buf, ",\"label\":");
    z_json_string_append(&buf, use.label ? use.label : "");
    zbuf_append(&buf, ",\"name\":");
    z_json_string_append(&buf, use.name ? use.name : "");
    zbuf_append(&buf, ",\"email\":");
    z_json_string_append(&buf, use.email ? use.email : "");
    zbuf_append(&buf, ",\"dir\":");
    z_json_string_append(&buf, use.dir ? use.dir : "");
    zbuf_append(&buf, "},\"active\":");
    zbuf_append(&buf, activate ? "true" : "false");
    zbuf_append(&buf, "}\n");
    fputs(buf.data, stdout);
    zbuf_free(&buf);
  } else {
    printf("imported key %s\n", use.id_hex ? use.id_hex : "");
    printf("path: %s\n", use.dir ? use.dir : "");
  }
  keys_use_free(&use);
  return 0;
}

static int keys_refresh_command(bool json) {
  ZDiag diag = {0};
  if (!trusted_keys_refresh(&diag)) return keys_error(&diag, "trusted-keys", json);
  if (json) {
    ZBuf buf;
    zbuf_init(&buf);
    zbuf_append(&buf, "{\"success\":true,\"trustedKeys\":true}\n");
    fputs(buf.data, stdout);
    zbuf_free(&buf);
  } else {
    printf("trusted keys updated\n");
  }
  return 0;
}

static bool ledger_keys_update(const char* ledger_path, const char* target_ref, const char* old_ref,
                               const char* new_ref, const char* reason, const char* mode, bool json) {
  ZDiag diag = {0};
  LedgerKeys trusted = {0};
  if (!trusted_keys_load_verified(&trusted, &diag)) {
    keys_error(&diag, "trusted-keys", json);
    return false;
  }
  LedgerKeys keys = {0};
  char* package_name = NULL;
  char* prev_hash = NULL;
  size_t prev_seq = 0;
  if (!ledger_read_keys(ledger_path, &trusted, &keys, &package_name, &prev_hash, &prev_seq, &diag)) {
    keys_error(&diag, ledger_path, json);
    ledger_keys_free(&trusted);
    return false;
  }
  char* root = keys_root_dir();
  KeyUse signer = {0};
  if (!keys_active_or_create(&signer, &diag)) {
    keys_error(&diag, "keys", json);
    ledger_keys_free(&trusted);
    ledger_keys_free(&keys);
    free(package_name);
    free(prev_hash);
    free(root);
    return false;
  }
  if (!trusted_keys_require_key(&trusted, signer.id_hex, signer.public_hex, false, &diag)) {
    keys_error(&diag, "trusted-keys", json);
    keys_use_free(&signer);
    ledger_keys_free(&trusted);
    ledger_keys_free(&keys);
    free(package_name);
    free(prev_hash);
    free(root);
    return false;
  }
  if (!z_ledger_keys_has_publisher(&keys, signer.id_hex) || z_ledger_keys_has_revoked(&keys, signer.id_hex)) {
    keys_error(&diag, ledger_path, json);
    keys_use_free(&signer);
    ledger_keys_free(&trusted);
    ledger_keys_free(&keys);
    free(package_name);
    free(prev_hash);
    free(root);
    return false;
  }
  if (strcmp(mode, "authorize") == 0) {
    KeyUse target = {0};
    if (!keys_use_by_ref(root, target_ref, false, &target, &diag)) {
      keys_error(&diag, "keys", json);
      keys_use_free(&target);
      keys_use_free(&signer);
      ledger_keys_free(&trusted);
      ledger_keys_free(&keys);
      free(package_name);
      free(prev_hash);
      free(root);
      return false;
    }
    if (!trusted_keys_require_key(&trusted, target.id_hex, target.public_hex, false, &diag)) {
      keys_error(&diag, "trusted-keys", json);
      keys_use_free(&target);
      keys_use_free(&signer);
      ledger_keys_free(&trusted);
      ledger_keys_free(&keys);
      free(package_name);
      free(prev_hash);
      free(root);
      return false;
    }
    if (!ledger_keys_add_publisher(&keys, &target, &diag)) {
      keys_error(&diag, ledger_path, json);
      keys_use_free(&target);
      keys_use_free(&signer);
      ledger_keys_free(&trusted);
      ledger_keys_free(&keys);
      free(package_name);
      free(prev_hash);
      free(root);
      return false;
    }
    keys_use_free(&target);
  } else if (strcmp(mode, "revoke") == 0) {
    KeyUse target = {0};
    if (!keys_use_by_ref(root, target_ref, false, &target, &diag)) {
      keys_error(&diag, "keys", json);
      keys_use_free(&target);
      keys_use_free(&signer);
      ledger_keys_free(&trusted);
      ledger_keys_free(&keys);
      free(package_name);
      free(prev_hash);
      free(root);
      return false;
    }
    if (!trusted_keys_require_key(&trusted, target.id_hex, target.public_hex, true, &diag)) {
      keys_error(&diag, "trusted-keys", json);
      keys_use_free(&target);
      keys_use_free(&signer);
      ledger_keys_free(&trusted);
      ledger_keys_free(&keys);
      free(package_name);
      free(prev_hash);
      free(root);
      return false;
    }
    if (!ledger_keys_add_revoked(&keys, target.id_hex, reason, &diag)) {
      keys_error(&diag, ledger_path, json);
      keys_use_free(&target);
      keys_use_free(&signer);
      ledger_keys_free(&trusted);
      ledger_keys_free(&keys);
      free(package_name);
      free(prev_hash);
      free(root);
      return false;
    }
    keys_use_free(&target);
  } else if (strcmp(mode, "rotate") == 0) {
    KeyUse old_key = {0};
    if (!keys_use_by_ref(root, old_ref, false, &old_key, &diag)) {
      keys_error(&diag, "keys", json);
      keys_use_free(&old_key);
      keys_use_free(&signer);
      ledger_keys_free(&trusted);
      ledger_keys_free(&keys);
      free(package_name);
      free(prev_hash);
      free(root);
      return false;
    }
    KeyUse add_key = {0};
    if (!keys_use_by_ref(root, new_ref, false, &add_key, &diag)) {
      keys_error(&diag, "keys", json);
      keys_use_free(&add_key);
      keys_use_free(&old_key);
      keys_use_free(&signer);
      ledger_keys_free(&trusted);
      ledger_keys_free(&keys);
      free(package_name);
      free(prev_hash);
      free(root);
      return false;
    }
    if (!trusted_keys_require_key(&trusted, add_key.id_hex, add_key.public_hex, false, &diag)) {
      keys_error(&diag, "trusted-keys", json);
      keys_use_free(&add_key);
      keys_use_free(&old_key);
      keys_use_free(&signer);
      ledger_keys_free(&trusted);
      ledger_keys_free(&keys);
      free(package_name);
      free(prev_hash);
      free(root);
      return false;
    }
    if (!z_ledger_keys_has_publisher(&keys, add_key.id_hex)) {
      if (!ledger_keys_add_publisher(&keys, &add_key, &diag)) {
        keys_error(&diag, ledger_path, json);
        keys_use_free(&add_key);
        keys_use_free(&old_key);
        keys_use_free(&signer);
        ledger_keys_free(&trusted);
        ledger_keys_free(&keys);
        free(package_name);
        free(prev_hash);
        free(root);
        return false;
      }
    }
    keys_use_free(&add_key);
    if (!z_ledger_keys_has_revoked(&keys, old_key.id_hex)) {
      if (!ledger_keys_add_revoked(&keys, old_key.id_hex, "rotated", &diag)) {
        keys_error(&diag, ledger_path, json);
        keys_use_free(&old_key);
        keys_use_free(&signer);
        ledger_keys_free(&trusted);
        ledger_keys_free(&keys);
        free(package_name);
        free(prev_hash);
        free(root);
        return false;
      }
    }
    keys_use_free(&old_key);
  }
  bool ok = ledger_write_append(ledger_path, package_name, &keys, &signer, prev_hash, prev_seq, &diag);
  if (!ok) {
    keys_error(&diag, ledger_path, json);
    keys_use_free(&signer);
    ledger_keys_free(&trusted);
    ledger_keys_free(&keys);
    free(package_name);
    free(prev_hash);
    free(root);
    return false;
  }
  if (json) {
    ZBuf buf;
    zbuf_init(&buf);
    zbuf_append(&buf, "{\"success\":true,\"ledger\":");
    z_json_string_append(&buf, ledger_path);
    zbuf_append(&buf, "}\n");
    fputs(buf.data, stdout);
    zbuf_free(&buf);
  } else {
    printf("updated ledger %s\n", ledger_path);
  }
  keys_use_free(&signer);
  ledger_keys_free(&trusted);
  ledger_keys_free(&keys);
  free(package_name);
  free(prev_hash);
  free(root);
  return true;
}

static bool keys_option_apply(const char* arg, bool* json) {
  if (strcmp(arg, "--json") == 0) {
    *json = true;
    return true;
  }
  if (strcmp(arg, "--insecure") == 0) {
    z_trusted_keys_insecure_set(true);
    return true;
  }
  return false;
}

static void keys_help_print(void) {
  printf("Usage: zero keys <command> [options]\n\n");
  printf("Commands:\n");
  printf("  list\n");
  printf("  show <label|key-id>\n");
  printf("  new --email <email> [--name <name>] [--label <label>] [--no-activate]\n");
  printf("  use <label|key-id>\n");
  printf("  import --public <hex> --email <email> [--name <name>] [--label <label>]\n");
  printf("  authorize --ledger <path> --key <label|key-id>\n");
  printf("  revoke --ledger <path> --key <label|key-id> --reason <text>\n");
  printf("  rotate --ledger <path> --old <label|key-id> --new <label|key-id>\n");
  printf("  refresh\n\n");
  printf("Flags:\n");
  printf("  --json\n");
  printf("  --insecure\n\n");
  printf("Environment:\n");
  printf("  ZERO_KEYS_DIR\n");
  printf("  ZERO_TRUSTED_KEYS_DIR\n");
  printf("  ZERO_TRUSTED_KEYS\n");
  printf("  ZERO_TRUSTED_KEYS_SIG\n");
  printf("  ZERO_TRUSTED_KEYS_URL\n");
  printf("  ZERO_TRUSTED_KEYS_SIG_URL\n");
}

int keys_command(int argc, char** argv) {
  bool json = false;
  int index = 2;
  for (; index < argc; index++) {
    if (keys_option_apply(argv[index], &json)) continue;
    if (strcmp(argv[index], "--help") == 0 || strcmp(argv[index], "-h") == 0) {
      keys_help_print();
      return 0;
    }
    break;
  }
  const char* kind = index < argc ? argv[index++] : "list";

  if (strcmp(kind, "list") == 0) {
    for (; index < argc; index++) {
      if (keys_option_apply(argv[index], &json)) continue;
      fprintf(stderr, "usage: zero keys list\n");
      return 1;
    }
    return keys_list_command(json);
  }

  if (strcmp(kind, "show") == 0) {
    const char* ref = index < argc ? argv[index++] : NULL;
    for (; index < argc; index++) {
      if (keys_option_apply(argv[index], &json)) continue;
      fprintf(stderr, "usage: zero keys show <label|key-id>\n");
      return 1;
    }
    if (!ref) {
      fprintf(stderr, "usage: zero keys show <label|key-id>\n");
      return 1;
    }
    return keys_show_command(ref, json);
  }

  if (strcmp(kind, "use") == 0) {
    const char* ref = index < argc ? argv[index++] : NULL;
    for (; index < argc; index++) {
      if (keys_option_apply(argv[index], &json)) continue;
      fprintf(stderr, "usage: zero keys use <label|key-id>\n");
      return 1;
    }
    if (!ref) {
      fprintf(stderr, "usage: zero keys use <label|key-id>\n");
      return 1;
    }
    return keys_use_command(ref, json);
  }

  if (strcmp(kind, "new") == 0) {
    const char* label = NULL;
    const char* name = NULL;
    const char* email = NULL;
    bool activate = true;
    for (; index < argc; index++) {
      if (keys_option_apply(argv[index], &json)) {
        continue;
      } else if (strcmp(argv[index], "--no-activate") == 0) {
        activate = false;
      } else if (strcmp(argv[index], "--label") == 0 && index + 1 < argc) {
        label = argv[++index];
      } else if (strcmp(argv[index], "--name") == 0 && index + 1 < argc) {
        name = argv[++index];
      } else if (strcmp(argv[index], "--email") == 0 && index + 1 < argc) {
        email = argv[++index];
      } else {
        fprintf(stderr, "usage: zero keys new --email <email> [--name <name>] [--label <label>] [--no-activate]\n");
        return 1;
      }
    }
    return keys_new_command(label, name, email, activate, json);
  }

  if (strcmp(kind, "import") == 0) {
    const char* label = NULL;
    const char* name = NULL;
    const char* email = NULL;
    const char* public_hex = NULL;
    for (; index < argc; index++) {
      if (keys_option_apply(argv[index], &json)) {
        continue;
      } else if (strcmp(argv[index], "--label") == 0 && index + 1 < argc) {
        label = argv[++index];
      } else if (strcmp(argv[index], "--name") == 0 && index + 1 < argc) {
        name = argv[++index];
      } else if (strcmp(argv[index], "--email") == 0 && index + 1 < argc) {
        email = argv[++index];
      } else if (strcmp(argv[index], "--public") == 0 && index + 1 < argc) {
        public_hex = argv[++index];
      } else {
        fprintf(stderr, "usage: zero keys import --public <hex> --email <email> [--name <name>] [--label <label>]\n");
        return 1;
      }
    }
    return keys_import_command(label, name, email, public_hex, false, json);
  }

  if (strcmp(kind, "refresh") == 0) {
    for (; index < argc; index++) {
      if (keys_option_apply(argv[index], &json)) continue;
      fprintf(stderr, "usage: zero keys refresh\n");
      return 1;
    }
    return keys_refresh_command(json);
  }

  if (strcmp(kind, "authorize") == 0) {
    const char* ledger_path = NULL;
    const char* key_ref = NULL;
    for (; index < argc; index++) {
      if (keys_option_apply(argv[index], &json)) {
        continue;
      } else if (strcmp(argv[index], "--ledger") == 0 && index + 1 < argc) {
        ledger_path = argv[++index];
      } else if (strcmp(argv[index], "--key") == 0 && index + 1 < argc) {
        key_ref = argv[++index];
      } else {
        fprintf(stderr, "usage: zero keys authorize --ledger <path> --key <label|key-id>\n");
        return 1;
      }
    }
    if (!ledger_path || !key_ref) {
      fprintf(stderr, "usage: zero keys authorize --ledger <path> --key <label|key-id>\n");
      return 1;
    }
    return ledger_keys_update(ledger_path, key_ref, NULL, NULL, NULL, "authorize", json) ? 0 : 1;
  }

  if (strcmp(kind, "revoke") == 0) {
    const char* ledger_path = NULL;
    const char* key_ref = NULL;
    const char* reason = NULL;
    for (; index < argc; index++) {
      if (keys_option_apply(argv[index], &json)) {
        continue;
      } else if (strcmp(argv[index], "--ledger") == 0 && index + 1 < argc) {
        ledger_path = argv[++index];
      } else if (strcmp(argv[index], "--key") == 0 && index + 1 < argc) {
        key_ref = argv[++index];
      } else if (strcmp(argv[index], "--reason") == 0 && index + 1 < argc) {
        reason = argv[++index];
      } else {
        fprintf(stderr, "usage: zero keys revoke --ledger <path> --key <label|key-id> --reason <text>\n");
        return 1;
      }
    }
    if (!ledger_path || !key_ref || !reason) {
      fprintf(stderr, "usage: zero keys revoke --ledger <path> --key <label|key-id> --reason <text>\n");
      return 1;
    }
    return ledger_keys_update(ledger_path, key_ref, NULL, NULL, reason, "revoke", json) ? 0 : 1;
  }

  if (strcmp(kind, "rotate") == 0) {
    const char* ledger_path = NULL;
    const char* old_ref = NULL;
    const char* new_ref = NULL;
    for (; index < argc; index++) {
      if (keys_option_apply(argv[index], &json)) {
        continue;
      } else if (strcmp(argv[index], "--ledger") == 0 && index + 1 < argc) {
        ledger_path = argv[++index];
      } else if (strcmp(argv[index], "--old") == 0 && index + 1 < argc) {
        old_ref = argv[++index];
      } else if (strcmp(argv[index], "--new") == 0 && index + 1 < argc) {
        new_ref = argv[++index];
      } else {
        fprintf(stderr, "usage: zero keys rotate --ledger <path> --old <label|key-id> --new <label|key-id>\n");
        return 1;
      }
    }
    if (!ledger_path || !old_ref || !new_ref) {
      fprintf(stderr, "usage: zero keys rotate --ledger <path> --old <label|key-id> --new <label|key-id>\n");
      return 1;
    }
    return ledger_keys_update(ledger_path, NULL, old_ref, new_ref, NULL, "rotate", json) ? 0 : 1;
  }

  fprintf(stderr, "zero keys: unknown command '%s'\n", kind);
  keys_help_print();
  return 1;
}
