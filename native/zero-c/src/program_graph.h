#ifndef ZERO_C_PROGRAM_GRAPH_H
#define ZERO_C_PROGRAM_GRAPH_H

#include "zero.h"

typedef enum {
  Z_PROGRAM_GRAPH_NODE_MODULE,
  Z_PROGRAM_GRAPH_NODE_IMPORT,
  Z_PROGRAM_GRAPH_NODE_C_IMPORT,
  Z_PROGRAM_GRAPH_NODE_CONST,
  Z_PROGRAM_GRAPH_NODE_TYPE_ALIAS,
  Z_PROGRAM_GRAPH_NODE_SHAPE,
  Z_PROGRAM_GRAPH_NODE_INTERFACE,
  Z_PROGRAM_GRAPH_NODE_ENUM,
  Z_PROGRAM_GRAPH_NODE_CHOICE,
  Z_PROGRAM_GRAPH_NODE_FUNCTION,
  Z_PROGRAM_GRAPH_NODE_PARAM,
  Z_PROGRAM_GRAPH_NODE_FIELD,
  Z_PROGRAM_GRAPH_NODE_ENUM_CASE,
  Z_PROGRAM_GRAPH_NODE_CHOICE_CASE,
  Z_PROGRAM_GRAPH_NODE_BLOCK,
  Z_PROGRAM_GRAPH_NODE_LET,
  Z_PROGRAM_GRAPH_NODE_ASSIGNMENT,
  Z_PROGRAM_GRAPH_NODE_DEFER,
  Z_PROGRAM_GRAPH_NODE_CHECK,
  Z_PROGRAM_GRAPH_NODE_RETURN,
  Z_PROGRAM_GRAPH_NODE_EXPRESSION_STATEMENT,
  Z_PROGRAM_GRAPH_NODE_IF,
  Z_PROGRAM_GRAPH_NODE_WHILE,
  Z_PROGRAM_GRAPH_NODE_FOR,
  Z_PROGRAM_GRAPH_NODE_BREAK,
  Z_PROGRAM_GRAPH_NODE_CONTINUE,
  Z_PROGRAM_GRAPH_NODE_MATCH,
  Z_PROGRAM_GRAPH_NODE_RAISE,
  Z_PROGRAM_GRAPH_NODE_MATCH_ARM,
  Z_PROGRAM_GRAPH_NODE_IDENTIFIER,
  Z_PROGRAM_GRAPH_NODE_LITERAL,
  Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS,
  Z_PROGRAM_GRAPH_NODE_INDEX_ACCESS,
  Z_PROGRAM_GRAPH_NODE_SLICE,
  Z_PROGRAM_GRAPH_NODE_CALL,
  Z_PROGRAM_GRAPH_NODE_METHOD_CALL,
  Z_PROGRAM_GRAPH_NODE_CAST,
  Z_PROGRAM_GRAPH_NODE_BORROW,
  Z_PROGRAM_GRAPH_NODE_RESCUE,
  Z_PROGRAM_GRAPH_NODE_META,
  Z_PROGRAM_GRAPH_NODE_SHAPE_LITERAL,
  Z_PROGRAM_GRAPH_NODE_ARRAY_LITERAL,
  Z_PROGRAM_GRAPH_NODE_FIELD_INIT,
  Z_PROGRAM_GRAPH_NODE_TYPE_REF,
  Z_PROGRAM_GRAPH_NODE_EFFECT_REF,
  Z_PROGRAM_GRAPH_NODE_ERROR_VARIANT,
  Z_PROGRAM_GRAPH_NODE_EXPRESSION,
  Z_PROGRAM_GRAPH_NODE_STATEMENT,
} ZProgramGraphNodeKind;

typedef enum {
  Z_PROGRAM_GRAPH_EDGE_TARGET_NODE,
  Z_PROGRAM_GRAPH_EDGE_TARGET_SYMBOL,
  Z_PROGRAM_GRAPH_EDGE_TARGET_TYPE,
  Z_PROGRAM_GRAPH_EDGE_TARGET_EFFECT,
} ZProgramGraphEdgeTarget;

typedef enum {
  Z_PROGRAM_GRAPH_VALIDATION_DECODED,
  Z_PROGRAM_GRAPH_VALIDATION_SHAPE_VALID,
  Z_PROGRAM_GRAPH_VALIDATION_RESOLVED,
  Z_PROGRAM_GRAPH_VALIDATION_TYPED,
  Z_PROGRAM_GRAPH_VALIDATION_EFFECT_CHECKED,
  Z_PROGRAM_GRAPH_VALIDATION_BUILDABLE,
} ZProgramGraphValidationState;

typedef struct {
  char *id;
  ZProgramGraphNodeKind kind;
  char *name;
  char *type;
  char *value;
  char *path;
  char *symbol_id;
  char *type_id;
  char *effect_id;
  char *node_hash;
  int line;
  int column;
  bool is_public;
  bool is_mutable;
  bool is_static;
  bool fallible;
  bool export_c;
} ZProgramGraphNode;

typedef struct {
  char *from;
  char *to;
  char *kind;
  ZProgramGraphEdgeTarget target;
  size_t order;
} ZProgramGraphEdge;

typedef struct {
  unsigned schema_version;
  ZProgramGraphValidationState validation_state;
  const char *id_strategy;
  char *module_identity;
  char *graph_hash;
  ZProgramGraphNode *nodes;
  size_t node_len, node_cap;
  ZProgramGraphEdge *edges;
  size_t edge_len, edge_cap, next_id;
  bool canonical_source;
} ZProgramGraph;

typedef struct {
  bool ok;
  ZProgramGraphValidationState state;
  char code[16];
  char message[160];
  char node_id[64];
  char edge_from[64];
  char edge_to[64];
  char edge_target[16];
} ZProgramGraphValidation;

const char *z_program_graph_node_kind_name(ZProgramGraphNodeKind kind);
const char *z_program_graph_edge_target_name(ZProgramGraphEdgeTarget target);
const char *z_program_graph_validation_state_name(ZProgramGraphValidationState state);
void z_program_graph_finalize_identities(ZProgramGraph *graph);
void z_program_graph_init(ZProgramGraph *graph);
void z_program_graph_free(ZProgramGraph *graph);
bool z_program_graph_validate(const ZProgramGraph *graph, ZProgramGraphValidation *validation);

#endif
