#ifndef ZERO_C_BUILDABILITY_H
#define ZERO_C_BUILDABILITY_H

#include "zero.h"

bool z_direct_buildability_check(const IrProgram *ir, const ZTargetInfo *target, const char *emit_kind, bool freestanding, ZDiag *diag);

#endif
