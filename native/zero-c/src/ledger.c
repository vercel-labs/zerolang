#include "ledger.h"
#include "crypto.h"
#include "hex.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool ledger_diag(ZDiag* diag, const char* path, const char* message) {
  diag->code = 9002;
  diag->path = path;
  diag->line = 1;
  diag->column = 1;
  snprintf(diag->message, sizeof(diag->message), "%s", message);
  snprintf(diag->help, sizeof(diag->help), "check ledger entries and keys");
  return false;
}

static void ledger_entry_hash(const char* text, size_t len, unsigned char hash[32]) {
  sha256_hash((const unsigned char*)text, len, hash);
}

static const char* ledger_text_or_empty(const char* text) {
  return text ? text : "";
}

static void ledger_keys_data_text(ZBuf* buf, const LedgerKeys* keys) {
  zbuf_append(buf, "{\n");
  zbuf_append(buf, "        \"publishers\": [\n");
  for (size_t i = 0; i < keys->publisher_count; i++) {
    const LedgerPublisher* publisher = &keys->publishers[i];
    zbuf_append(buf, "          {\n");
    zbuf_append(buf, "            \"keyId\": ");
    z_json_string_append(buf, ledger_text_or_empty(publisher->key_id));
    zbuf_append(buf, ",\n            \"publicKey\": ");
    z_json_string_append(buf, ledger_text_or_empty(publisher->public_key));
    zbuf_append(buf, ",\n            \"label\": ");
    z_json_string_append(buf, ledger_text_or_empty(publisher->label));
    zbuf_append(buf, ",\n            \"name\": ");
    z_json_string_append(buf, ledger_text_or_empty(publisher->name));
    zbuf_append(buf, ",\n            \"email\": ");
    z_json_string_append(buf, ledger_text_or_empty(publisher->email));
    zbuf_append(buf, "\n          }");
    if (i + 1 < keys->publisher_count) zbuf_append(buf, ",");
    zbuf_append(buf, "\n");
  }
  zbuf_append(buf, "        ],\n");
  zbuf_appendf(buf, "        \"threshold\": %d,\n", keys->threshold);
  zbuf_append(buf, "        \"revoked\": ");
  if (keys->revoked_count == 0) {
    zbuf_append(buf, "[]\n");
  } else {
    zbuf_append(buf, "[\n");
    for (size_t i = 0; i < keys->revoked_count; i++) {
      const LedgerRevoked* item = &keys->revoked[i];
      zbuf_append(buf, "          {\n");
      zbuf_append(buf, "            \"keyId\": ");
      z_json_string_append(buf, ledger_text_or_empty(item->key_id));
      zbuf_append(buf, ",\n            \"reason\": ");
      z_json_string_append(buf, ledger_text_or_empty(item->reason));
      zbuf_append(buf, "\n          }");
      if (i + 1 < keys->revoked_count) zbuf_append(buf, ",");
      zbuf_append(buf, "\n");
    }
    zbuf_append(buf, "        ]\n");
  }
  zbuf_append(buf, "      }");
}

static bool ledger_keys_entry_sign(const unsigned char hash[32], const KeyUse* signer, unsigned char signature[64]) {
  return ed25519_sign(signature, hash, 32, signer->public_key, signer->private_key);
}

static bool ledger_entry_build(ZBuf* entry, const LedgerKeys* keys, const KeyUse* signer, const char* prev_hash,
                               size_t seq, ZDiag* diag) {
  ZBuf data;
  zbuf_init(&data);
  ledger_keys_data_text(&data, keys);

  unsigned char hash_bytes[32];
  ledger_entry_hash(data.data ? data.data : "", data.len, hash_bytes);
  char hash_hex[65];
  z_hex_write(hash_bytes, 32, hash_hex);

  unsigned char sig_bytes[64];
  if (!ledger_keys_entry_sign(hash_bytes, signer, sig_bytes)) {
    zbuf_free(&data);
    return ledger_diag(diag, NULL, "ledger sign failed");
  }
  char sig_hex[129];
  z_hex_write(sig_bytes, 64, sig_hex);

  zbuf_append(entry, "    {\n");
  zbuf_append(entry, "      \"kind\": \"keys\",\n");
  zbuf_appendf(entry, "      \"seq\": %zu,\n", seq);
  zbuf_append(entry, "      \"prevHash\": ");
  z_json_string_append(entry, prev_hash ? prev_hash : "");
  zbuf_append(entry, ",\n      \"hash\": ");
  z_json_string_append(entry, hash_hex);
  zbuf_append(entry, ",\n      \"data\": ");
  zbuf_append(entry, data.data ? data.data : "{}");
  zbuf_append(entry, ",\n      \"signatures\": [\n        {\n          \"keyId\": ");
  z_json_string_append(entry, signer->id_hex ? signer->id_hex : "");
  zbuf_append(entry, ",\n          \"sig\": ");
  z_json_string_append(entry, sig_hex);
  zbuf_append(entry, "\n        }\n      ]\n    }");

  zbuf_free(&data);
  return true;
}

static bool json_array_last_entry(const char* array, const char** start_out, const char** end_out, bool* has_entries) {
  const char* cursor = z_json_whitespace_skip(array);
  if (*cursor != '[') return false;
  cursor++;
  const char* last_start = NULL;
  const char* last_end = NULL;
  while (*cursor) {
    cursor = z_json_whitespace_skip(cursor);
    if (*cursor == ']') break;
    const char* value_end = z_json_value_skip(cursor);
    if (!value_end) return false;
    last_start = cursor;
    last_end = value_end;
    cursor = z_json_whitespace_skip(value_end);
    if (*cursor == ',') {
      cursor++;
      continue;
    }
    if (*cursor == ']') break;
    return false;
  }
  if (has_entries) *has_entries = last_start != NULL;
  if (!last_start || !last_end) return false;
  *start_out = last_start;
  *end_out = last_end;
  return true;
}

static bool ledger_publishers_read(const char* data, LedgerKeys* keys, ZDiag* diag) {
  const char* path[] = {"publishers"};
  const char* start = NULL;
  const char* end = NULL;
  if (!z_json_span_path_get(data, path, 1, &start, &end)) return ledger_diag(diag, NULL, "ledger publishers missing");
  const char* cursor = z_json_whitespace_skip(start);
  if (*cursor != '[') return ledger_diag(diag, NULL, "ledger publishers invalid");
  cursor++;
  while (*cursor) {
    cursor = z_json_whitespace_skip(cursor);
    if (*cursor == ']') break;
    const char* value_end = z_json_value_skip(cursor);
    if (!value_end) return ledger_diag(diag, NULL, "ledger publishers invalid");
    char* publisher_text = z_json_span_copy(cursor, value_end);
    if (!publisher_text) return ledger_diag(diag, NULL, "ledger publishers invalid");
    const char* key_path[] = {"keyId"};
    const char* public_path[] = {"publicKey"};
    const char* label_path[] = {"label"};
    const char* name_path[] = {"name"};
    const char* email_path[] = {"email"};
    char* key_id = z_json_string_path_get(publisher_text, key_path, 1);
    char* public_key = z_json_string_path_get(publisher_text, public_path, 1);
    char* label = z_json_string_path_get(publisher_text, label_path, 1);
    char* name = z_json_string_path_get(publisher_text, name_path, 1);
    char* email = z_json_string_path_get(publisher_text, email_path, 1);
    free(publisher_text);
    if (!key_id || !public_key) {
      free(key_id);
      free(public_key);
      free(label);
      free(name);
      free(email);
      return ledger_diag(diag, NULL, "ledger publishers missing fields");
    }
    if (!label) label = z_strdup("");
    if (!name) name = z_strdup("");
    if (!email) email = z_strdup("");
    LedgerPublisher* next = realloc(keys->publishers, (keys->publisher_count + 1) * sizeof(LedgerPublisher));
    if (!next) {
      free(key_id);
      free(public_key);
      free(label);
      free(name);
      free(email);
      return ledger_diag(diag, NULL, "ledger publishers allocation failed");
    }
    keys->publishers = next;
    keys->publishers[keys->publisher_count++] =
        (LedgerPublisher){.key_id = key_id, .public_key = public_key, .label = label, .name = name, .email = email};
    cursor = z_json_whitespace_skip(value_end);
    if (*cursor == ',') {
      cursor++;
      continue;
    }
    if (*cursor == ']') break;
    return ledger_diag(diag, NULL, "ledger publishers invalid");
  }
  return true;
}

static bool ledger_revoked_read(const char* data, LedgerKeys* keys, ZDiag* diag) {
  const char* path[] = {"revoked"};
  const char* start = NULL;
  const char* end = NULL;
  if (!z_json_span_path_get(data, path, 1, &start, &end)) {
    keys->revoked = NULL;
    keys->revoked_count = 0;
    return true;
  }
  const char* cursor = z_json_whitespace_skip(start);
  if (*cursor != '[') return ledger_diag(diag, NULL, "ledger revoked invalid");
  cursor++;
  while (*cursor) {
    cursor = z_json_whitespace_skip(cursor);
    if (*cursor == ']') break;
    const char* value_end = z_json_value_skip(cursor);
    if (!value_end) return ledger_diag(diag, NULL, "ledger revoked invalid");
    LedgerRevoked item = {0};
    if (*cursor == '"') {
      char* value = z_json_string_parse_copy(cursor, &cursor);
      if (!value) return ledger_diag(diag, NULL, "ledger revoked invalid");
      item.key_id = value;
      item.reason = z_strdup("");
    } else {
      char* value_text = z_json_span_copy(cursor, value_end);
      if (!value_text) return ledger_diag(diag, NULL, "ledger revoked invalid");
      const char* key_path[] = {"keyId"};
      const char* reason_path[] = {"reason"};
      char* key_id = z_json_string_path_get(value_text, key_path, 1);
      char* reason = z_json_string_path_get(value_text, reason_path, 1);
      free(value_text);
      if (!key_id) {
        free(reason);
        return ledger_diag(diag, NULL, "ledger revoked missing keyId");
      }
      if (!reason) reason = z_strdup("");
      item.key_id = key_id;
      item.reason = reason;
    }
    LedgerRevoked* next = realloc(keys->revoked, (keys->revoked_count + 1) * sizeof(LedgerRevoked));
    if (!next) {
      free(item.key_id);
      free(item.reason);
      return ledger_diag(diag, NULL, "ledger revoked allocation failed");
    }
    keys->revoked = next;
    keys->revoked[keys->revoked_count++] = item;
    cursor = z_json_whitespace_skip(value_end);
    if (*cursor == ',') {
      cursor++;
      continue;
    }
    if (*cursor == ']') break;
    return ledger_diag(diag, NULL, "ledger revoked invalid");
  }
  return true;
}

static bool ledger_threshold_read(const char* data, LedgerKeys* keys, ZDiag* diag) {
  const char* path[] = {"threshold"};
  const char* start = NULL;
  const char* end = NULL;
  if (!z_json_span_path_get(data, path, 1, &start, &end)) return ledger_diag(diag, NULL, "ledger threshold missing");
  long long value = 0;
  if (!z_json_number_from_span(start, end, &value)) return ledger_diag(diag, NULL, "ledger threshold invalid");
  keys->threshold = (int)value;
  return true;
}

bool z_ledger_keys_from_json_data(const char* json, const char* path, LedgerKeys* out, ZDiag* diag) {
  if (!ledger_publishers_read(json, out, diag) || !ledger_revoked_read(json, out, diag) ||
      !ledger_threshold_read(json, out, diag)) {
    return false;
  }
  if (out->threshold < 1 || out->publisher_count < (size_t)out->threshold) {
    ledger_keys_free(out);
    return ledger_diag(diag, path, "ledger threshold invalid");
  }
  for (size_t i = 0; i < out->publisher_count; i++) {
    LedgerPublisher* publisher = &out->publishers[i];
    if (!publisher->key_id || !publisher->key_id[0]) {
      ledger_keys_free(out);
      return ledger_diag(diag, path, "ledger key id missing");
    }
    if (!publisher->public_key || !publisher->public_key[0]) {
      ledger_keys_free(out);
      return ledger_diag(diag, path, "ledger public key missing");
    }
    unsigned char public_bytes[32];
    if (!z_hex_read(publisher->public_key, 32, public_bytes)) {
      ledger_keys_free(out);
      return ledger_diag(diag, path, "ledger public key invalid");
    }
    unsigned char key_id_bytes[32];
    z_key_id_from_public(public_bytes, key_id_bytes);
    char key_id_hex[65];
    z_hex_write(key_id_bytes, 32, key_id_hex);
    if (strcmp(key_id_hex, publisher->key_id) != 0) {
      ledger_keys_free(out);
      return ledger_diag(diag, path, "ledger key id mismatch");
    }
  }
  return true;
}

static const LedgerPublisher* ledger_publisher_find(const LedgerKeys* keys, const char* key_id) {
  for (size_t i = 0; i < keys->publisher_count; i++) {
    if (strcmp(keys->publishers[i].key_id, key_id) == 0) return &keys->publishers[i];
  }
  return NULL;
}

static bool ledger_revoked_has(const LedgerKeys* keys, const char* key_id) {
  for (size_t i = 0; i < keys->revoked_count; i++) {
    if (strcmp(keys->revoked[i].key_id, key_id) == 0) return true;
  }
  return false;
}

bool z_ledger_keys_has_publisher(const LedgerKeys* keys, const char* key_id) {
  return ledger_publisher_find(keys, key_id) != NULL;
}

bool z_ledger_keys_has_revoked(const LedgerKeys* keys, const char* key_id) {
  return ledger_revoked_has(keys, key_id);
}

static bool ledger_keys_require_trusted(const LedgerKeys* keys, const LedgerKeys* trusted_keys, const char* path,
                                        ZDiag* diag) {
  if (!trusted_keys || trusted_keys->publisher_count == 0) {
    return ledger_diag(diag, path, "trusted keys missing");
  }
  for (size_t i = 0; i < keys->publisher_count; i++) {
    const LedgerPublisher* publisher = &keys->publishers[i];
    const LedgerPublisher* trusted = ledger_publisher_find(trusted_keys, publisher->key_id);
    if (!trusted) return ledger_diag(diag, path, "ledger key not trusted");
    if (ledger_revoked_has(trusted_keys, publisher->key_id)) return ledger_diag(diag, path, "trusted key revoked");
    if (strcmp(trusted->public_key, publisher->public_key) != 0) return ledger_diag(diag, path, "trusted key mismatch");
  }
  for (size_t i = 0; i < keys->revoked_count; i++) {
    if (!ledger_publisher_find(trusted_keys, keys->revoked[i].key_id)) {
      return ledger_diag(diag, path, "ledger revoked key unknown");
    }
  }
  return true;
}

static bool ledger_signer_seen(char** items, size_t count, const char* key_id) {
  for (size_t i = 0; i < count; i++) {
    if (strcmp(items[i], key_id) == 0) return true;
  }
  return false;
}

static void ledger_signer_list_free(char** items, size_t count) {
  for (size_t i = 0; i < count; i++) free(items[i]);
  free(items);
}

static bool ledger_signatures_verify(const char* signatures_text, const LedgerKeys* authorized_keys,
                                     const LedgerKeys* trusted_keys, const unsigned char hash_bytes[32], int threshold,
                                     const char* path, ZDiag* diag) {
  if (threshold < 1) return ledger_diag(diag, path, "ledger threshold invalid");
  const char* cursor = z_json_whitespace_skip(signatures_text);
  if (*cursor != '[') return ledger_diag(diag, path, "ledger signatures invalid");
  cursor++;
  char** seen = NULL;
  size_t seen_count = 0;
  size_t valid_count = 0;
  while (*cursor) {
    cursor = z_json_whitespace_skip(cursor);
    if (*cursor == ']') break;
    const char* value_end = z_json_value_skip(cursor);
    if (!value_end) {
      ledger_signer_list_free(seen, seen_count);
      return ledger_diag(diag, path, "ledger signatures invalid");
    }
    char* signature_text = z_json_span_copy(cursor, value_end);
    if (!signature_text) {
      ledger_signer_list_free(seen, seen_count);
      return ledger_diag(diag, path, "ledger signatures invalid");
    }
    const char* key_path[] = {"keyId"};
    const char* sig_path[] = {"sig"};
    char* key_id = z_json_string_path_get(signature_text, key_path, 1);
    char* sig_hex = z_json_string_path_get(signature_text, sig_path, 1);
    free(signature_text);
    if (!key_id || !sig_hex) {
      free(key_id);
      free(sig_hex);
      ledger_signer_list_free(seen, seen_count);
      return ledger_diag(diag, path, "ledger signatures missing fields");
    }
    const LedgerPublisher* publisher = ledger_publisher_find(authorized_keys, key_id);
    if (!publisher) {
      free(key_id);
      free(sig_hex);
      ledger_signer_list_free(seen, seen_count);
      return ledger_diag(diag, path, "ledger signer not authorized");
    }
    if (ledger_revoked_has(authorized_keys, key_id) || ledger_revoked_has(trusted_keys, key_id)) {
      free(key_id);
      free(sig_hex);
      ledger_signer_list_free(seen, seen_count);
      return ledger_diag(diag, path, "ledger signer revoked");
    }
    unsigned char signature[64];
    if (!z_hex_read(sig_hex, 64, signature)) {
      free(key_id);
      free(sig_hex);
      ledger_signer_list_free(seen, seen_count);
      return ledger_diag(diag, path, "ledger signature invalid");
    }
    unsigned char public_bytes[32];
    if (!z_hex_read(publisher->public_key, 32, public_bytes)) {
      free(key_id);
      free(sig_hex);
      ledger_signer_list_free(seen, seen_count);
      return ledger_diag(diag, path, "ledger signer key invalid");
    }
    if (!ed25519_verify(signature, hash_bytes, 32, public_bytes)) {
      free(key_id);
      free(sig_hex);
      ledger_signer_list_free(seen, seen_count);
      return ledger_diag(diag, path, "ledger signature invalid");
    }
    if (!ledger_signer_seen(seen, seen_count, key_id)) {
      char** next = realloc(seen, (seen_count + 1) * sizeof(char*));
      if (!next) {
        free(key_id);
        free(sig_hex);
        ledger_signer_list_free(seen, seen_count);
        return ledger_diag(diag, path, "ledger signature allocation failed");
      }
      seen = next;
      seen[seen_count++] = z_strdup(key_id);
      valid_count++;
    }
    free(key_id);
    free(sig_hex);
    cursor = z_json_whitespace_skip(value_end);
    if (*cursor == ',') {
      cursor++;
      continue;
    }
    if (*cursor == ']') break;
    ledger_signer_list_free(seen, seen_count);
    return ledger_diag(diag, path, "ledger signatures invalid");
  }
  if (valid_count < (size_t)threshold) {
    ledger_signer_list_free(seen, seen_count);
    return ledger_diag(diag, path, "ledger signatures insufficient");
  }
  ledger_signer_list_free(seen, seen_count);
  return true;
}

bool ledger_read_keys(const char* path, const LedgerKeys* trusted_keys, LedgerKeys* out, char** package_name,
                      char** prev_hash, size_t* prev_seq, ZDiag* diag) {
  if (!trusted_keys || trusted_keys->publisher_count == 0) return ledger_diag(diag, path, "trusted keys missing");
  char* text = z_read_file(path, diag);
  if (!text) return false;
  const char* entries_path[] = {"entries"};
  const char* entries_start = NULL;
  const char* entries_end = NULL;
  if (!z_json_span_path_get(text, entries_path, 1, &entries_start, &entries_end)) {
    free(text);
    return ledger_diag(diag, path, "ledger entries missing");
  }
  const char* cursor = z_json_whitespace_skip(entries_start);
  if (*cursor != '[') {
    free(text);
    return ledger_diag(diag, path, "ledger entries invalid");
  }
  cursor++;
  bool has_entries = false;
  LedgerKeys current_keys = {0};
  char* current_hash = NULL;
  size_t current_seq = 0;
  while (*cursor) {
    cursor = z_json_whitespace_skip(cursor);
    if (*cursor == ']') break;
    const char* value_end = z_json_value_skip(cursor);
    if (!value_end) {
      ledger_keys_free(&current_keys);
      free(current_hash);
      free(text);
      return ledger_diag(diag, path, "ledger entries invalid");
    }
    char* entry_text = z_json_span_copy(cursor, value_end);
    if (!entry_text) {
      ledger_keys_free(&current_keys);
      free(current_hash);
      free(text);
      return ledger_diag(diag, path, "ledger entry missing");
    }
    const char* seq_path[] = {"seq"};
    const char* prev_path[] = {"prevHash"};
    const char* hash_path[] = {"hash"};
    const char* data_path[] = {"data"};
    const char* sig_path[] = {"signatures"};
    const char* seq_start = NULL;
    const char* seq_end = NULL;
    if (!z_json_span_path_get(entry_text, seq_path, 1, &seq_start, &seq_end)) {
      free(entry_text);
      ledger_keys_free(&current_keys);
      free(current_hash);
      free(text);
      return ledger_diag(diag, path, "ledger seq missing");
    }
    long long seq_value = 0;
    if (!z_json_number_from_span(seq_start, seq_end, &seq_value) || seq_value < 1) {
      free(entry_text);
      ledger_keys_free(&current_keys);
      free(current_hash);
      free(text);
      return ledger_diag(diag, path, "ledger seq invalid");
    }
    char* prev_hash_text = z_json_string_path_get(entry_text, prev_path, 1);
    char* hash_text = z_json_string_path_get(entry_text, hash_path, 1);
    if (!prev_hash_text || !hash_text) {
      free(prev_hash_text);
      free(hash_text);
      free(entry_text);
      ledger_keys_free(&current_keys);
      free(current_hash);
      free(text);
      return ledger_diag(diag, path, "ledger hash missing");
    }
    const char* data_start = NULL;
    const char* data_end = NULL;
    if (!z_json_span_path_get(entry_text, data_path, 1, &data_start, &data_end)) {
      free(prev_hash_text);
      free(hash_text);
      free(entry_text);
      ledger_keys_free(&current_keys);
      free(current_hash);
      free(text);
      return ledger_diag(diag, path, "ledger data missing");
    }
    char* data_text = z_json_span_copy(data_start, data_end);
    if (!data_text) {
      free(prev_hash_text);
      free(hash_text);
      free(entry_text);
      ledger_keys_free(&current_keys);
      free(current_hash);
      free(text);
      return ledger_diag(diag, path, "ledger data missing");
    }
    const char* sig_start = NULL;
    const char* sig_end = NULL;
    if (!z_json_span_path_get(entry_text, sig_path, 1, &sig_start, &sig_end)) {
      free(prev_hash_text);
      free(hash_text);
      free(data_text);
      free(entry_text);
      ledger_keys_free(&current_keys);
      free(current_hash);
      free(text);
      return ledger_diag(diag, path, "ledger signatures missing");
    }
    char* sig_text = z_json_span_copy(sig_start, sig_end);
    if (!sig_text) {
      free(prev_hash_text);
      free(hash_text);
      free(data_text);
      free(entry_text);
      ledger_keys_free(&current_keys);
      free(current_hash);
      free(text);
      return ledger_diag(diag, path, "ledger signatures missing");
    }
    if ((size_t)seq_value != current_seq + 1) {
      free(prev_hash_text);
      free(hash_text);
      free(data_text);
      free(sig_text);
      free(entry_text);
      ledger_keys_free(&current_keys);
      free(current_hash);
      free(text);
      return ledger_diag(diag, path, "ledger seq invalid");
    }
    if (seq_value == 1) {
      if (prev_hash_text[0] != 0) {
        free(prev_hash_text);
        free(hash_text);
        free(data_text);
        free(sig_text);
        free(entry_text);
        ledger_keys_free(&current_keys);
        free(current_hash);
        free(text);
        return ledger_diag(diag, path, "ledger prev hash invalid");
      }
    } else {
      if (!current_hash || strcmp(prev_hash_text, current_hash) != 0) {
        free(prev_hash_text);
        free(hash_text);
        free(data_text);
        free(sig_text);
        free(entry_text);
        ledger_keys_free(&current_keys);
        free(current_hash);
        free(text);
        return ledger_diag(diag, path, "ledger prev hash invalid");
      }
    }
    unsigned char hash_bytes[32];
    sha256_hash((const unsigned char*)data_text, strlen(data_text), hash_bytes);
    char hash_hex[65];
    z_hex_write(hash_bytes, 32, hash_hex);
    if (strcmp(hash_hex, hash_text) != 0) {
      free(prev_hash_text);
      free(hash_text);
      free(data_text);
      free(sig_text);
      free(entry_text);
      ledger_keys_free(&current_keys);
      free(current_hash);
      free(text);
      return ledger_diag(diag, path, "ledger hash mismatch");
    }
    LedgerKeys entry_keys = {0};
    if (!z_ledger_keys_from_json_data(data_text, path, &entry_keys, diag)) {
      free(prev_hash_text);
      free(hash_text);
      free(data_text);
      free(sig_text);
      free(entry_text);
      ledger_keys_free(&current_keys);
      free(current_hash);
      free(text);
      return false;
    }
    if (!ledger_keys_require_trusted(&entry_keys, trusted_keys, path, diag)) {
      ledger_keys_free(&entry_keys);
      free(prev_hash_text);
      free(hash_text);
      free(data_text);
      free(sig_text);
      free(entry_text);
      ledger_keys_free(&current_keys);
      free(current_hash);
      free(text);
      return false;
    }
    const LedgerKeys* authorized_keys = seq_value == 1 ? trusted_keys : &current_keys;
    int threshold = seq_value == 1 ? trusted_keys->threshold : current_keys.threshold;
    if (!ledger_signatures_verify(sig_text, authorized_keys, trusted_keys, hash_bytes, threshold, path, diag)) {
      ledger_keys_free(&entry_keys);
      free(prev_hash_text);
      free(hash_text);
      free(data_text);
      free(sig_text);
      free(entry_text);
      ledger_keys_free(&current_keys);
      free(current_hash);
      free(text);
      return false;
    }
    ledger_keys_free(&current_keys);
    current_keys = entry_keys;
    free(current_hash);
    current_hash = hash_text;
    current_seq = (size_t)seq_value;
    has_entries = true;
    free(prev_hash_text);
    free(data_text);
    free(sig_text);
    free(entry_text);
    cursor = z_json_whitespace_skip(value_end);
    if (*cursor == ',') {
      cursor++;
      continue;
    }
    if (*cursor == ']') break;
    ledger_keys_free(&current_keys);
    free(current_hash);
    free(text);
    return ledger_diag(diag, path, "ledger entries invalid");
  }
  if (!has_entries) {
    ledger_keys_free(&current_keys);
    free(current_hash);
    free(text);
    return ledger_diag(diag, path, "ledger entries missing");
  }
  if (package_name) {
    const char* pkg_path[] = {"package", "name"};
    *package_name = z_json_string_path_get(text, pkg_path, 2);
    if (!*package_name) {
      ledger_keys_free(&current_keys);
      free(current_hash);
      free(text);
      return ledger_diag(diag, path, "ledger package name missing");
    }
  }
  *out = current_keys;
  if (prev_hash)
    *prev_hash = current_hash;
  else
    free(current_hash);
  if (prev_seq) *prev_seq = current_seq;
  free(text);
  return true;
}

bool ledger_write_new(const char* root, const char* package, const LedgerKeys* keys, const KeyUse* signer,
                      ZDiag* diag) {
  ZBuf entry;
  zbuf_init(&entry);
  if (!ledger_entry_build(&entry, keys, signer, "", 1, diag)) {
    zbuf_free(&entry);
    return false;
  }
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, "{\n  \"schemaVersion\": 1,\n  \"format\": \"zero-ledger-v1\",\n  \"package\": { \"name\": ");
  z_json_string_append(&buf, package);
  zbuf_append(&buf, " },\n  \"entries\": [\n");
  zbuf_append(&buf, entry.data ? entry.data : "{}");
  zbuf_append(&buf, "\n  ]\n}\n");

  char* path = z_path_join(root, "ledger.json");
  bool ok = z_write_file(path, buf.data ? buf.data : "", diag);
  free(path);
  zbuf_free(&entry);
  zbuf_free(&buf);
  return ok;
}

bool ledger_write_append(const char* path, const char* package, const LedgerKeys* keys, const KeyUse* signer,
                         const char* prev_hash, size_t prev_seq, ZDiag* diag) {
  char* text = z_read_file(path, diag);
  if (!text) return false;
  const char* entries_path[] = {"entries"};
  const char* entries_start = NULL;
  const char* entries_end = NULL;
  if (!z_json_span_path_get(text, entries_path, 1, &entries_start, &entries_end)) {
    free(text);
    return ledger_diag(diag, path, "ledger entries missing");
  }
  bool has_entries = false;
  const char* last_start = NULL;
  const char* last_end = NULL;
  if (!json_array_last_entry(entries_start, &last_start, &last_end, &has_entries)) {
    free(text);
    return ledger_diag(diag, path, "ledger entries invalid");
  }
  ZBuf entry;
  zbuf_init(&entry);
  if (!ledger_entry_build(&entry, keys, signer, prev_hash ? prev_hash : "", prev_seq + 1, diag)) {
    zbuf_free(&entry);
    free(text);
    return false;
  }
  const char* entries_close = entries_end - 1;
  const char* prefix_end = entries_close;
  while (prefix_end > text && isspace((unsigned char)prefix_end[-1])) prefix_end--;
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_appendf(&buf, "%.*s", (int)(prefix_end - text), text);
  if (has_entries) zbuf_append(&buf, ",\n");
  zbuf_append(&buf, entry.data ? entry.data : "{}");
  zbuf_append(&buf, "\n  ");
  zbuf_append(&buf, entries_close);
  zbuf_free(&entry);
  bool ok = z_write_file(path, buf.data ? buf.data : "", diag);
  zbuf_free(&buf);
  free(text);
  (void)package;
  return ok;
}

void ledger_keys_free(LedgerKeys* keys) {
  if (!keys) return;
  for (size_t i = 0; i < keys->publisher_count; i++) {
    free(keys->publishers[i].key_id);
    free(keys->publishers[i].public_key);
    free(keys->publishers[i].label);
    free(keys->publishers[i].name);
    free(keys->publishers[i].email);
  }
  free(keys->publishers);
  for (size_t i = 0; i < keys->revoked_count; i++) {
    free(keys->revoked[i].key_id);
    free(keys->revoked[i].reason);
  }
  free(keys->revoked);
  keys->publishers = NULL;
  keys->publisher_count = 0;
  keys->revoked = NULL;
  keys->revoked_count = 0;
  keys->threshold = 0;
}
