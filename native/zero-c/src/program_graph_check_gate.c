#include "program_graph_check_gate.h"
#include <string.h>

/*
 * Check-time buildability gate facts: `zero check` and `zero import` fail
 * with the same BLD004 "typed graph MIR unsupported" diagnostics `zero
 * build`/`zero run` would report when the typed graph cannot lower to MIR at
 * all. Target-specific backend buildability subsets (for example fs values
 * on the Mach-O direct backend), target/backend availability, and toolchain
 * readiness stay target-readiness facts only, so packages that build for
 * another supported target keep checking clean on every host.
 */
bool z_check_gate_diag_is_buildability_blocker(const ZDiag *diag) {
  if (!diag) return false;
  if (diag->code != 2004 && diag->code != 4004) return false;
  if (!diag->backend_blocker.present) return false;
  return strcmp(diag->backend_blocker.stage, "lower") == 0;
}

static bool check_gate_type_decl_is_enum_or_choice(const ZProgramGraph *graph, const char *name) {
  if (!graph || !name || !name[0]) return false;
  for (size_t i = 0; i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind != Z_PROGRAM_GRAPH_NODE_ENUM && node->kind != Z_PROGRAM_GRAPH_NODE_CHOICE) continue;
    if (node->name && strcmp(node->name, name) == 0) return true;
  }
  return false;
}

/*
 * The gate starts with the typed graph MIR gaps agents hit most often, so
 * conformance check-pass coverage for the remaining gaps stays valid while
 * each gap either gains lowering support or migrates to a check-failure
 * contract. Every gated construct fails `zero build` identically today; the
 * list only controls how much of that build signal check surfaces early.
 */
bool z_check_gate_diag_is_known_construct(const ZProgramGraph *graph, const ZDiag *diag) {
  if (!diag) return false;
  const char *message = diag->message;
  const char *actual = diag->actual;
  if (strcmp(message, "typed graph MIR statement kind is unsupported") == 0) return strcmp(actual, "Defer") == 0;
  if (strcmp(message, "typed graph MIR local type is unsupported") == 0) return check_gate_type_decl_is_enum_or_choice(graph, actual);
  if (strcmp(message, "typed graph MIR rescue supports fallible function calls with primitive fallbacks") == 0) return true;
  if (strcmp(message, "typed graph MIR literal type is unsupported") == 0) return strcmp(actual, "String") == 0;
  if (strcmp(message, "typed graph MIR parameter type is unsupported") == 0) return strcmp(actual, "ref<ByteBuf>") == 0;
  if (strcmp(message, "typed graph MIR reference parameter requires a shape whose fields are scalars or fixed scalar arrays") == 0) return strcmp(actual, "ref<ByteBuf>") == 0;
  if (strcmp(message, "typed graph MIR call target is unsupported") == 0) return strcmp(actual, "readU32") == 0;
  if (strcmp(message, "typed graph MIR allocator local requires FixedBufAlloc") == 0) return strcmp(actual, "PageAlloc") == 0 || strcmp(actual, "GeneralAlloc") == 0;
  return false;
}

/*
 * Fast filter: the gate only pays for merged lowering when the graph can
 * contain a gated construct at all. One node pass keeps construct-free
 * programs at pre-gate check and import cost.
 */
bool z_check_gate_scan_finds_known_construct(const ZProgramGraph *graph) {
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    switch (node->kind) {
      case Z_PROGRAM_GRAPH_NODE_DEFER:
      case Z_PROGRAM_GRAPH_NODE_RESCUE:
      case Z_PROGRAM_GRAPH_NODE_ENUM:
      case Z_PROGRAM_GRAPH_NODE_CHOICE:
        return true;
      case Z_PROGRAM_GRAPH_NODE_LET:
        if (node->type && (strcmp(node->type, "PageAlloc") == 0 || strcmp(node->type, "GeneralAlloc") == 0)) return true;
        break;
      case Z_PROGRAM_GRAPH_NODE_PARAM:
        if (node->type && strcmp(node->type, "ref<ByteBuf>") == 0) return true;
        break;
      case Z_PROGRAM_GRAPH_NODE_IDENTIFIER:
      case Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS:
        if (node->name && strcmp(node->name, "readU32") == 0) return true;
        break;
      default:
        break;
    }
  }
  return false;
}
