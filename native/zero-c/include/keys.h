#ifndef ZERO_C_KEYS_H
#define ZERO_C_KEYS_H

#include "zero.h"

typedef struct LedgerKeys LedgerKeys;

typedef struct {
  unsigned char public_key[32];
  unsigned char private_key[64];
  unsigned char key_id[32];
  char* id_hex;
  char* public_hex;
  char* dir;
  char* label;
  char* name;
  char* email;
  bool created;
  bool has_private;
} KeyUse;

void z_key_id_from_public(const unsigned char public_key[32], unsigned char key_id[32]);
void z_trusted_keys_insecure_set(bool enabled);
bool z_trusted_keys_load_verified(LedgerKeys* out, ZDiag* diag);
bool z_trusted_keys_allow_key(const KeyUse* use, ZDiag* diag);
bool keys_active_or_create(KeyUse* out, ZDiag* diag);
void keys_use_free(KeyUse* use);

int keys_command(int argc, char** argv);

#endif
