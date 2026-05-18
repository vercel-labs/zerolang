#ifndef ZERO_C_PACKAGE_H
#define ZERO_C_PACKAGE_H

#include "zero.h"

typedef enum { PACKAGE_TEMPLATE_CLI, PACKAGE_TEMPLATE_LIB, PACKAGE_TEMPLATE_APP } PackageTemplate;

typedef struct {
  char* key_id;
  char* key_dir;
  char* public_key;
  bool key_created;
} PackageKeyNote;

bool package_create(const char* root, const char* name, PackageTemplate template_kind, PackageKeyNote* note,
                    ZDiag* diag);
void package_key_note_free(PackageKeyNote* note);

#endif
