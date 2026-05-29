#ifndef ZERO_SAFETY_CONTRACT_H
#define ZERO_SAFETY_CONTRACT_H

#include "zero.h"

typedef struct {
  const char *canonical_profile;
  const char *profile_key;
} ZSafetyFactsProfile;

void z_append_safety_facts_json(ZBuf *buf, const ZSafetyFactsProfile *profile);

#endif
