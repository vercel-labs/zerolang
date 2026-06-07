#include "safety_contract.h"

static void safety_append_json_string(ZBuf *buf, const char *value) {
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

void z_append_safety_facts_json(ZBuf *buf, const ZSafetyFactsProfile *profile) {
  zbuf_append(buf, "{\"schemaVersion\":1,\"profile\":");
  safety_append_json_string(buf, profile ? profile->canonical_profile : "release-small");
  zbuf_append(buf, ",\"profileKey\":");
  safety_append_json_string(buf, profile ? profile->profile_key : "small");
  zbuf_append(buf, ",\"coverage\":\"checker-mir-direct-backends\",\"bounds\":{\"policy\":\"checked\"");
  zbuf_append(buf, ",\"staticDiagnostics\":true,\"runtimeTraps\":true,\"trapRuntime\":\"pay-as-used\",\"optimizerElision\":");
  zbuf_append(buf, "false");
  zbuf_append(buf, ",\"unchecked\":false},\"overflow\":{\"policy\":\"literal-range-checked-runtime-unchecked\"");
  zbuf_append(buf, ",\"integerLiterals\":\"range-checked\",\"staticValues\":\"range-checked\"");
  zbuf_append(buf, ",\"runtimeArithmetic\":\"unchecked-machine-wrap\",\"unchecked\":true}");
  zbuf_append(buf, ",\"initialization\":{\"locals\":\"initializer-required\",\"fields\":\"explicit-or-default-required\",\"fixedArrays\":\"literal-or-repeat-required\",\"maybePayloadReads\":\"guard-checked\",\"unchecked\":false}");
  zbuf_append(buf, ",\"aliasing\":{\"mutableAliases\":\"diagnostic\",\"sharedWhileMutable\":\"diagnostic\",\"references\":\"provenance-checked\"}");
  zbuf_append(buf, ",\"lifetimes\":{\"escapedLocalBorrow\":\"diagnostic\",\"returnedLocalBorrow\":\"diagnostic\",\"borrowedStdlibResults\":\"provenance-checked\"}");
  zbuf_append(buf, ",\"ownership\":{\"moves\":\"checked\",\"useAfterMove\":\"diagnostic\",\"ownedResourceDrop\":\"explicit\"}");
  zbuf_append(buf, ",\"spans\":{\"construction\":\"type-and-mutability-checked\",\"indexing\":\"bounds-checked\",\"slicing\":\"order-and-bounds-checked\"}");
  zbuf_append(buf, ",\"mir\":{\"contracts\":\"verified-before-direct-emission\",\"localLayout\":true,\"localIndexBounds\":true,\"memoryOpWidths\":true,\"invalidMemoryContractsBlockEmission\":true}");
  zbuf_append(buf, ",\"uncheckedSurfaces\":[");
  zbuf_append(buf, "{\"surface\":\"C imports\",\"policy\":\"externally-trusted\"}");
  zbuf_append(buf, ",{\"surface\":\"host filesystem/process/network effects\",\"policy\":\"capability-gated-not-sandboxed\"}");
  zbuf_append(buf, "],\"productionReadiness\":");
  z_append_production_readiness_json(buf, profile);
  zbuf_append(buf, "}");
}

void z_append_production_readiness_json(ZBuf *buf, const ZSafetyFactsProfile *profile) {
  zbuf_append(buf, "{\"schemaVersion\":1,\"profile\":");
  safety_append_json_string(buf, profile ? profile->canonical_profile : "release-small");
  zbuf_append(buf, ",\"profileKey\":");
  safety_append_json_string(buf, profile ? profile->profile_key : "small");
  zbuf_append(buf, ",\"sensitiveEnvironmentApproved\":false");
  zbuf_append(buf, ",\"status\":\"blocked\"");
  zbuf_append(buf, ",\"gate\":\"sensitive-production-v1\"");
  zbuf_append(buf, ",\"satisfiedControls\":[");
  zbuf_append(buf, "\"compile-time-filesystem-denied\",\"compile-time-network-denied\",\"compile-time-process-denied\"");
  zbuf_append(buf, ",\"bounds-checks-retained\",\"initializer-required\",\"borrow-and-move-diagnostics\",\"mir-contracts-block-invalid-emission\"");
  zbuf_append(buf, "]");
  zbuf_append(buf, ",\"blockingRisks\":[");
  zbuf_append(buf, "{\"id\":\"runtime-integer-overflow-unchecked\",\"severity\":\"high\",\"surface\":\"runtime arithmetic\",\"requiredControl\":\"checked or explicitly proven overflow semantics\"}");
  zbuf_append(buf, ",{\"id\":\"c-imports-externally-trusted\",\"severity\":\"high\",\"surface\":\"C imports\",\"requiredControl\":\"audited ABI boundary and dependency trust policy\"}");
  zbuf_append(buf, ",{\"id\":\"host-effects-not-sandboxed\",\"severity\":\"high\",\"surface\":\"filesystem process network effects\",\"requiredControl\":\"runtime confinement or deployment policy bound to capabilities\"}");
  zbuf_append(buf, ",{\"id\":\"release-sbom-placeholder\",\"severity\":\"medium\",\"surface\":\"ship artifacts\",\"requiredControl\":\"complete dependency and license SBOM\"}");
  zbuf_append(buf, "]");
  zbuf_append(buf, ",\"nextRequiredControls\":[");
  zbuf_append(buf, "\"checked-runtime-overflow-or-proof-carrying-arithmetic\"");
  zbuf_append(buf, ",\"audited-c-interop-contracts\"");
  zbuf_append(buf, ",\"runtime-capability-confinement\"");
  zbuf_append(buf, ",\"complete-sbom-generation\"");
  zbuf_append(buf, "]}");
}
