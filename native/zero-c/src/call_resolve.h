#ifndef ZERO_C_CALL_RESOLVE_H
#define ZERO_C_CALL_RESOLVE_H

#include "zero.h"

typedef enum {
  Z_CALL_UNKNOWN,
  Z_CALL_FUNCTION,
  Z_CALL_STDLIB,
  Z_CALL_RECEIVER,
  Z_CALL_SHAPE_NAMESPACE,
  Z_CALL_CONSTRAINED_INTERFACE,
  Z_CALL_CONCRETE_CONSTRAINED_SHAPE,
  Z_CALL_CHOICE_CONSTRUCTOR
} ZCallKind;

typedef struct {
  ZCallKind kind;
  const Expr *call_expr;
  const Expr *callee_expr;
  const Expr *receiver_expr;
  const TypeArgVec *type_args;
  const Function *callee;
  const Shape *shape;
  const InterfaceDecl *interface;
  const Choice *choice;
  const Param *choice_case;
  size_t param_offset;
  char *callee_name;
  char *return_type;
  char *effect_summary_key;
  bool fallible;
} ZCallResolution;

void z_call_resolution_init(ZCallResolution *resolution);
void z_call_resolution_free(ZCallResolution *resolution);
void z_call_resolution_set_callee_name(ZCallResolution *resolution, const char *name);
void z_call_resolution_set_return_type(ZCallResolution *resolution, const char *return_type);
void z_call_resolution_set_effect_summary_key(ZCallResolution *resolution, const char *key);
const char *z_call_kind_name(ZCallKind kind);

#endif
