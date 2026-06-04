#include "zero.h"

static void metadata_append_json_string(ZBuf *buf, const char *value) {
  zbuf_append_char(buf, '"');
  for (const char *cursor = value ? value : ""; *cursor; cursor++) {
    unsigned char ch = (unsigned char)*cursor;
    if (ch == '"') zbuf_append(buf, "\\\"");
    else if (ch == '\\') zbuf_append(buf, "\\\\");
    else if (ch == '\n') zbuf_append(buf, "\\n");
    else if (ch == '\r') zbuf_append(buf, "\\r");
    else if (ch == '\t') zbuf_append(buf, "\\t");
    else if (ch < 0x20) zbuf_appendf(buf, "\\u%04x", (unsigned)ch);
    else zbuf_append_char(buf, (char)ch);
  }
  zbuf_append_char(buf, '"');
}

const char *z_llvm_backend_lifecycle_json_text(void) {
  return "{\"stability\":\"experimental\",\"defaultEligible\":false,\"releaseEligible\":false,\"shipEligible\":false,\"fallbackEligible\":false,\"supportedReleaseFamily\":\"direct\",\"reason\":\"LLVM is explicit experimental build/run/IR only; direct emitters remain the supported release path\"}";
}

void z_append_llvm_backend_lifecycle_json(ZBuf *buf) {
  zbuf_append(buf, z_llvm_backend_lifecycle_json_text());
}

void z_append_llvm_backend_lifecycle_field_json(ZBuf *buf) {
  zbuf_append(buf, ",\"backendLifecycle\":");
  z_append_llvm_backend_lifecycle_json(buf);
}

void z_append_doctor_llvm_toolchain_json(ZBuf *buf, const ZTargetInfo *host_target, const ZLlvmToolchainPlan *plan) {
  zbuf_append(buf, ",\n  \"llvmToolchain\": {\"status\":");
  metadata_append_json_string(buf, plan ? plan->status : "unsupported-target");
  zbuf_append(buf, ",\"target\":");
  metadata_append_json_string(buf, host_target && host_target->name ? host_target->name : z_host_target());
  zbuf_append(buf, ",\"driverKind\":\"clang\",\"selectionSource\":");
  metadata_append_json_string(buf, plan ? plan->selection_source : "path");
  zbuf_append(buf, ",\"compiler\":");
  metadata_append_json_string(buf, plan ? plan->compiler : "clang");
  zbuf_append(buf, ",\"targetTriple\":");
  metadata_append_json_string(buf, plan ? plan->target_triple : "unknown-unknown-unknown");
  zbuf_append(buf, ",\"nativeExecutable\":");
  zbuf_append(buf, plan && plan->native_executable ? "true" : "false");
  z_append_llvm_backend_lifecycle_field_json(buf);
  zbuf_append(buf, ",\"reason\":");
  metadata_append_json_string(buf, plan ? plan->reason : "host target is not supported by native LLVM executable builds");
  zbuf_append(buf, "}");
}
