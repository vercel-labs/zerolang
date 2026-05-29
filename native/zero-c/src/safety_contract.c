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
  zbuf_append(buf, ",\"initialization\":{\"locals\":\"initializer-required\",\"fields\":\"explicit-or-default-required\",\"fixedArrays\":\"literal-or-repeat-required\",\"maybePayloadReads\":\"type-checked\",\"unchecked\":false}");
  zbuf_append(buf, ",\"aliasing\":{\"mutableAliases\":\"diagnostic\",\"sharedWhileMutable\":\"diagnostic\",\"references\":\"provenance-checked\"}");
  zbuf_append(buf, ",\"lifetimes\":{\"escapedLocalBorrow\":\"diagnostic\",\"returnedLocalBorrow\":\"diagnostic\",\"borrowedStdlibResults\":\"provenance-checked\"}");
  zbuf_append(buf, ",\"ownership\":{\"moves\":\"checked\",\"useAfterMove\":\"diagnostic\",\"ownedResourceDrop\":\"explicit\"}");
  zbuf_append(buf, ",\"spans\":{\"construction\":\"type-and-mutability-checked\",\"indexing\":\"bounds-checked\",\"slicing\":\"order-and-bounds-checked\"}");
  zbuf_append(buf, ",\"mir\":{\"contracts\":\"verified-before-direct-emission\",\"localLayout\":true,\"localIndexBounds\":true,\"memoryOpWidths\":true,\"invalidMemoryContractsBlockEmission\":true}");
  zbuf_append(buf, ",\"uncheckedSurfaces\":[");
  zbuf_append(buf, "{\"surface\":\"C imports\",\"policy\":\"externally-trusted\"}");
  zbuf_append(buf, ",{\"surface\":\"host filesystem/process/network effects\",\"policy\":\"capability-gated-not-sandboxed\"}");
  zbuf_append(buf, "]}");
}
