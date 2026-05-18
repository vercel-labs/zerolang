#ifndef ZERO_C_LEDGER_H
#define ZERO_C_LEDGER_H

#include "keys.h"

typedef struct {
  char* key_id;
  char* public_key;
  char* label;
  char* name;
  char* email;
} LedgerPublisher;

typedef struct {
  char* key_id;
  char* reason;
} LedgerRevoked;

typedef struct LedgerKeys {
  LedgerPublisher* publishers;
  size_t publisher_count;
  LedgerRevoked* revoked;
  size_t revoked_count;
  int threshold;
} LedgerKeys;

bool ledger_write_new(const char* root, const char* package, const LedgerKeys* keys, const KeyUse* signer, ZDiag* diag);
bool z_ledger_keys_from_json_data(const char* json, const char* path, LedgerKeys* out, ZDiag* diag);
bool z_ledger_keys_has_publisher(const LedgerKeys* keys, const char* key_id);
bool z_ledger_keys_has_revoked(const LedgerKeys* keys, const char* key_id);
bool ledger_read_keys(const char* path, const LedgerKeys* trusted_keys, LedgerKeys* out, char** package_name,
                      char** prev_hash, size_t* prev_seq, ZDiag* diag);
bool ledger_write_append(const char* path, const char* package, const LedgerKeys* keys, const KeyUse* signer,
                         const char* prev_hash, size_t prev_seq, ZDiag* diag);
void ledger_keys_free(LedgerKeys* keys);

#endif
