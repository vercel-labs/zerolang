#include "zero.h"

#include <stdio.h>
#include <string.h>

ZBackendFamily z_backend_family_from_request(const char *requested_backend, const char *emit_kind) {
  if (requested_backend && requested_backend[0]) {
    if (strcmp(requested_backend, "direct") == 0) return Z_BACKEND_FAMILY_DIRECT;
    if (strcmp(requested_backend, "llvm") == 0) return Z_BACKEND_FAMILY_LLVM;
    if (z_direct_backend_is_request_name(requested_backend)) return Z_BACKEND_FAMILY_DIRECT;
    return Z_BACKEND_FAMILY_UNKNOWN;
  }
  return emit_kind && strcmp(emit_kind, "llvm-ir") == 0 ? Z_BACKEND_FAMILY_LLVM : Z_BACKEND_FAMILY_DIRECT;
}

const char *z_backend_family_name(ZBackendFamily family) {
  switch (family) {
    case Z_BACKEND_FAMILY_DIRECT: return "direct";
    case Z_BACKEND_FAMILY_LLVM: return "llvm";
    case Z_BACKEND_FAMILY_UNKNOWN:
    default:
      return "unknown";
  }
}

bool z_backend_request_is_known(const char *requested_backend, const char *emit_kind) {
  return z_backend_family_from_request(requested_backend, emit_kind) != Z_BACKEND_FAMILY_UNKNOWN;
}

bool z_backend_request_is_llvm(const char *requested_backend, const char *emit_kind) {
  return z_backend_family_from_request(requested_backend, emit_kind) == Z_BACKEND_FAMILY_LLVM;
}

const char *z_backend_direct_request_name(const char *requested_backend) {
  return z_direct_backend_is_request_name(requested_backend) ? requested_backend : NULL;
}

const char *z_backend_request_expected(void) {
  return "one of direct, llvm, or a direct emitter name";
}

void z_backend_init_unknown_diag(ZDiag *diag, const char *requested_backend, const char *path) {
  memset(diag, 0, sizeof(*diag));
  diag->code = 2002;
  diag->path = path;
  diag->line = 1;
  diag->column = 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "unknown backend '%s'", requested_backend ? requested_backend : "");
  snprintf(diag->expected, sizeof(diag->expected), "%s", z_backend_request_expected());
  snprintf(diag->actual, sizeof(diag->actual), "--backend %s", requested_backend ? requested_backend : "");
  snprintf(diag->help, sizeof(diag->help), "omit --backend for direct output, use --backend direct, or request the explicit llvm backend");
}

void z_backend_init_llvm_unavailable_diag(ZDiag *diag, const ZTargetInfo *target, const char *emit_kind, const char *path) {
  memset(diag, 0, sizeof(*diag));
  diag->code = 2004;
  diag->path = path;
  diag->line = 1;
  diag->column = 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "LLVM backend is recognized but not available");
  snprintf(diag->expected, sizeof(diag->expected), "available backend family for --emit %s", emit_kind ? emit_kind : "exe");
  snprintf(diag->actual, sizeof(diag->actual), "backend=llvm emit=%s", emit_kind ? emit_kind : "exe");
  snprintf(diag->help, sizeof(diag->help), "omit --backend or use --backend direct; LLVM IR emission is not implemented yet");
  ZBackendBlocker blocker;
  z_backend_blocker_set(&blocker,
                        target && target->name ? target->name : "unknown",
                        target && target->object_format ? target->object_format : "unknown",
                        "llvm",
                        "backend-selection",
                        "llvm backend unavailable");
  z_diag_set_backend_blocker(diag, &blocker);
}
