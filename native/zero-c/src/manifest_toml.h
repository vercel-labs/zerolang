#ifndef ZERO_MANIFEST_TOML_H
#define ZERO_MANIFEST_TOML_H

#include "zero.h"

bool z_parse_manifest_toml(const char *manifest, ZManifest *out, ZDiag *diag);

#endif
