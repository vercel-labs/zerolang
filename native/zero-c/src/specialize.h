#ifndef ZERO_C_SPECIALIZE_H
#define ZERO_C_SPECIALIZE_H

#include "zero.h"

typedef enum {
  Z_SPECIALIZATION_ADD_ADDED,
  Z_SPECIALIZATION_ADD_PRESENT,
  Z_SPECIALIZATION_ADD_NAME_COLLISION,
  Z_SPECIALIZATION_ADD_LIMIT
} ZSpecializationAddResult;

typedef struct {
  const Function *source;
  TypeArgVec type_args;
  char *name;
} ZSpecializationEntry;

typedef struct {
  ZSpecializationEntry *items;
  size_t len;
  size_t cap;
  size_t limit;
  bool hit_limit;
} ZSpecializationPlan;

void z_specialization_plan_init(ZSpecializationPlan *plan, size_t limit);
void z_specialization_plan_free(ZSpecializationPlan *plan);
char *z_specialized_function_name(const Function *fun, const TypeArgVec *type_args);
ZSpecializationAddResult z_specialization_plan_add(ZSpecializationPlan *plan, const Function *source, const TypeArgVec *type_args, char **out_name);

#endif
